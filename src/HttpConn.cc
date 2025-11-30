#include "HttpConn.h"
#include "Common.h"
#include "ApiRegister.h"
#include "ApiLogin.h"
#include "ApiMyfiles.h"
#include "ApiSharefiles.h"
#include "ApiDealfile.h"
#include "ApiDealsharefile.h"
#include "ApiSharepicture.h"
#include "ApiMd5.h"
#include "ApiUpload.h"

static HttpConnMap_t g_http_conn_map;

extern string strMsfdUrl;
extern string strDiscovery;

// conn_handle 从0开始递增，可以防止因socket_handle重用引起的一些冲突
static uint32_t g_conn_handle_generator = 0;

typedef std::unordered_map<uint32_t, CHttpConn *> UserMap_t;

static UserMap_t g_uuid_conn_map;

ThreadPool g_thread_pool;
uint32_t CHttpConn::s_uuid_alloctor = 0;

std::mutex CHttpConn::s_resp_mutex;
std::list<ResponsePdu_t *> CHttpConn::s_response_pdu_list;

CHttpConn *FindHttpConnByHandle(uint32_t conn_handle)
{
    CHttpConn *pConn = nullptr;
    auto it = g_http_conn_map.find(conn_handle);
    if (it != g_http_conn_map.end())
    {
        pConn = it->second;
    }
    return pConn;
}

CHttpConn *FindHttpConnByuuid(uint32_t uuid)
{
    CHttpConn *pConn = nullptr;
    auto it = g_uuid_conn_map.find(uuid);
    if (it != g_uuid_conn_map.end())
    {
        pConn = it->second;
    }
    return pConn;
}

void http_conn_callback(void *callback_data, uint8_t msg, uint32_t handle, uint32_t uParam, void *pParam)
{
    UNUSED(uParam);
    UNUSED(pParam);

    //
    uint32_t conn_handle = *((uint32_t *)(&callback_data)); // to ask;
    CHttpConn *pConn = FindHttpConnByHandle(conn_handle);
    if (!pConn)
    {
        return;
    }

    switch (msg)
    {
    case NETLIB_MSG_READ:
        pConn->OnRead();
        break;
    case NETLIB_MSG_WRITE:
        pConn->OnWrite();
        break;
    case NETLIB_MSG_CLOSE:
        pConn->OnClose();
        break;
    default:
        LOG_ERROR << "httpconn_callback error, msg: " << msg;
        break;
    }
}

void http_conn_timer_callback(void *callback_data, uint8_t msg, uint32_t handle, void *pParam)
{
    UNUSED(pParam);

    CHttpConn *pConn = nullptr;
    uint64_t cur_time = GetTickCount();
    HttpConnMap_t::iterator it, it_old;

    for (it = g_http_conn_map.begin(); it != g_http_conn_map.end();)
    {
        it_old = it;
        it++;
        pConn = it_old->second;
        pConn->OnTimer(cur_time);
    }
}

void init_http_conn()
{
    netlib_register_timer(http_conn_timer_callback, NULL, 1000);
}

//////////////////
CHttpConn::CHttpConn()
{
    m_busy = false;
    m_state = CONN_STATE_IDLE;
    m_sock_handle = NETLIB_INVALID_HANDLE;
    m_conn_handle = ++g_conn_handle_generator;
    m_last_send_tick = m_last_recv_tick = GetTickCount();

    if (m_conn_handle == 0)
    {
        m_conn_handle = ++g_conn_handle_generator;
    }

    m_uuid = ++CHttpConn::s_uuid_alloctor;
    // 判断为0是因为怕uint32溢出，溢出的化会变成0，而0可能会导致其它意想不到的错误
    if (m_uuid == 0)
    {
        m_uuid = ++CHttpConn::s_uuid_alloctor;
    }

    g_uuid_conn_map.insert(std::make_pair(m_uuid, this));
}

CHttpConn::~CHttpConn()
{
}

int CHttpConn::Send(void *data, int len)
{
    m_last_send_tick = GetTickCount();

    if (m_busy)
    {
        m_out_buffer.Write(data, len);
        return len;
    }

    int ret = netlib_send(m_sock_handle, data, len);
    if (ret < 0)
        ret = 0;
    if (ret < len)
    {
        m_out_buffer.Write((char *)data + ret, len - ret);
        m_busy = true;
        LOG_INFO << "not send all, remain= " << m_out_buffer.GetWriteOffset();
    }
    else
    {
        OnWriteComplete();
    }
    return len;
}

void CHttpConn::Close()
{
    m_state = CONN_STATE_CLOSED;

    g_uuid_conn_map.erase(m_uuid);
    g_http_conn_map.erase(m_conn_handle);
    netlib_close(m_sock_handle);

    ReleaseRef();
}
void CHttpConn::OnConnect(net_handle_t handle)
{
    m_sock_handle = handle;
    m_state = CONN_STATE_CONNECTED;
    g_http_conn_map.insert(std::make_pair(m_conn_handle, this));

    netlib_option(m_sock_handle, NETLIB_OPT_SET_CALLBACK, (void *)http_conn_callback);
    netlib_option(m_sock_handle, NETLIB_OPT_SET_CALLBACK_DATA, reinterpret_cast<void *>(m_conn_handle));
    netlib_option(m_sock_handle, NETLIB_OPT_GET_REMOTE_IP, (void *)&m_peer_ip);
}

void CHttpConn::OnRead()
{
    for (;;)
    {
        uint32_t free_buf_len = m_in_buffer.GetAllocSize() - m_in_buffer.GetWriteOffset();
        if (free_buf_len < READ_BUF_SIZE + 1)
        {
            m_in_buffer.Extend(READ_BUF_SIZE + 1);
        }
        // GetBuffer获取缓冲区下标
        int ret = netlib_recv(m_sock_handle, m_in_buffer.GetBuffer() + m_in_buffer.GetWriteOffset(), READ_BUF_SIZE);
        if (ret < 0)
            break;
        m_in_buffer.IncWriteOffset(ret);
        m_last_recv_tick = GetTickCount();
    }

    // 每次请求对应一个http连接，所以读完数据后，不用会再在同一个连接中读取下一个请求
    char *in_buf = (char *)m_in_buffer.GetBuffer();
    uint32_t buf_len = m_in_buffer.GetWriteOffset();
    in_buf[buf_len] = '\0';

    // 正常url最大值为2048，我们接收的所有数据长度不应超过这个数，
    if (buf_len > 2048)
    {
        LOG_ERROR << "get too much data";
        Close();
        return;
    }
    // LOG_DEBUG << "buf_len: " << buf_len << ", m_conn_handle: " << m_conn_handle << ", in_buf:" << in_buf;
    //  解析http数据
    m_cHttpParser.ParseHttpContent(in_buf, buf_len);
    if (m_cHttpParser.IsReadAll())
    {
        string url = m_cHttpParser.GetUrl();
        string content = m_cHttpParser.GetBodyContent();
        LOG_INFO << "url: " << url;
        if (strncmp(url.c_str(), api_reg, strlen(api_reg)) == 0)
        {
            _HandleRegisterRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_log, strlen(api_log)) == 0)
        {
            _HandleLoginRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_myflies, strlen(api_myflies)) == 0)
        {
            _HandleMyfilesRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_md5, strlen(api_md5)) == 0)
        {
            _HandleMd5Request(url, content);
        }
        else if (strncmp(url.c_str(), api_upload, strlen(api_upload)) == 0)
        {
            _HandleUploadRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_sharefiles, strlen(api_sharefiles)) == 0)
        {
            _HandleSharefilesRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_dealfile, strlen(api_dealfile)) == 0)
        {
            _HandleDealfileRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_dealsharefile, strlen(api_dealsharefile)) == 0)
        {
            _HandleDealsharefileRequest(url, content);
        }
        else if (strncmp(url.c_str(), api_sharepic, strlen(api_sharepic)) == 0)
        {
            _HandleSharepictureRequest(url, content);
        }
        else
        {
            LOG_ERROR << "url unknown, url= " << url;
            Close();
        }
    }
}
void CHttpConn::OnWrite() // to ask?
{
    if (!m_busy)
    {
        return;
    }
    int ret = netlib_send(m_sock_handle, m_out_buffer.GetBuffer(), m_out_buffer.GetWriteOffset());
    if (ret < 0)
    {
        ret = 0;
    }
    int out_buf_size = (int)m_out_buffer.GetWriteOffset();
    m_out_buffer.Read(NULL, ret); // 头部读出ret字节，并丢弃，因为这里只需要把指针前移

    if (ret < out_buf_size)
    {
        m_busy = true;
        LOG_INFO << "not send all, remain= " << m_out_buffer.GetWriteOffset();
    }
    else
    {
        OnWriteComplete();
        m_busy = false;
    }
}
void CHttpConn::OnClose()
{
    Close();
}
void CHttpConn::OnTimer(uint64_t curr_tick)
{
    if (curr_tick > m_last_recv_tick + HTTP_CCONN_TIMEOUT)
    {
        LOG_WARN << "HttpConn timeout, handle= " << m_conn_handle;
        Close();
    }
}
void CHttpConn::OnWriteComplete()
{
    LOG_INFO << "write complete";
    Close();
}

// 文件上传处理
int CHttpConn::_HandleUploadRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiUpload, m_uuid, url, post_data);
    return 0;
}
// 账号注册处理
int CHttpConn::_HandleRegisterRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiRegisterUser, m_uuid, url, post_data);
    return 0;
}
// 账号登录处理
int CHttpConn::_HandleLoginRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiUserLogin, m_uuid, url, post_data);
    return 0;
}
// 处理文件，共享/删除
int CHttpConn::_HandleDealfileRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiDealfile, m_uuid, url, post_data);
    return 0;
}
// 取消分享/转存/更新下载计数
int CHttpConn::_HandleDealsharefileRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiDealsharefile, m_uuid, url, post_data);
    return 0;
}
// 秒传文件处理，上传文件时检查该md5是否已经存在
int CHttpConn::_HandleMd5Request(string &url, string &post_data)
{
    g_thread_pool.exec(ApiMd5, m_uuid, url, post_data);
    return 0;
}
// 获取个人上传文件
int CHttpConn::_HandleMyfilesRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiMyfiles, m_uuid, url, post_data);

    return 0;
}
// 获取共享文件或下载榜
int CHttpConn::_HandleSharefilesRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiSharefiles, m_uuid, url, post_data);
    return 0;
}
// 分享图片
int CHttpConn::_HandleSharepictureRequest(string &url, string &post_data)
{
    g_thread_pool.exec(ApiSharepicture, m_uuid, url, post_data);
    return 0;
}

void CHttpConn::AddResponseData(uint32_t conn_uuid, string resp_data)
{
    LOG_INFO << "into";
    ResponsePdu_t *pResp = new ResponsePdu_t;
    pResp->conn_uuid = conn_uuid;
    pResp->resp_data = std::move(resp_data);

    s_resp_mutex.lock();
    s_response_pdu_list.push_back(pResp);
    s_resp_mutex.unlock();
}
void CHttpConn::SendResponseDataList()
{
    s_resp_mutex.lock();
    while (!s_response_pdu_list.empty())
    {
        ResponsePdu_t *pResp = s_response_pdu_list.front();
        s_response_pdu_list.pop_front();
        s_resp_mutex.unlock();

        CHttpConn *pConn = FindHttpConnByuuid(pResp->conn_uuid);
        LOG_INFO << "conn_uuid: " << pResp->conn_uuid << ", pConn: " << pConn;
        if (pConn)
        {
            LOG_INFO << "send: " << pResp->resp_data;
            pConn->Send((void *)pResp->resp_data.c_str(), pResp->resp_data.length());
        }
        delete pResp;
        s_resp_mutex.lock();
    }
    s_resp_mutex.unlock();
}