#pragma once

#include <list>
#include <mutex>
#include <unordered_map>
#include "netlib.h"
#include "util.h"
#include "HttpParserWrapper.h"
#include "ThreadPool.h"

#define HTTP_CCONN_TIMEOUT 60000

#define READ_BUF_SIZE 2048

#define HTTP_RESPONSE_HTML "HTTP/1.1 200 OK\r\n"   \
                           "Connection:close\r\n"  \
                           "Content-Length:%d\r\n" \
                           "Content-Type:application/json;charset=utf-8\r\n\r\n%s"

#define HTTP_RESPONSE_HTML_MAX 4096

constexpr const char *api_reg = "/api/reg";
constexpr const char *api_log = "/api/login";
constexpr const char *api_myflies = "/api/myfiles";
constexpr const char *api_md5 = "/api/md5";
constexpr const char *api_upload = "/api/upload";
constexpr const char *api_sharefiles = "/api/sharefiles";
constexpr const char *api_dealfile = "/api/dealfile";
constexpr const char *api_dealsharefile = "/api/dealsharefile";
constexpr const char *api_sharepic = "/api/sharepic";

extern ThreadPool g_thread_pool;

enum
{
    CONN_STATE_IDLE,
    CONN_STATE_CONNECTED,
    CONN_STATE_OPEN,
    CONN_STATE_CLOSED,
};

typedef struct
{
    uint32_t conn_uuid; // 查找connection
    string resp_data;   // 要回发的数据
} ResponsePdu_t;

class CHttpConn : public CRefObject
{
public:
    CHttpConn();
    virtual ~CHttpConn();

    uint32_t GetHttpHandle() { return m_conn_handle; }
    char *GetPeerIp() { return (char *)m_peer_ip.c_str(); }

    int Send(void *data, int len);

    void Close();
    void OnConnect(net_handle_t handle);
    void OnRead();
    void OnWrite();
    void OnClose();
    void OnTimer(uint64_t curr_tick);
    void OnWriteComplete();

    static void AddResponseData(uint32_t conn_uuid, string resp_data); // 工作线程调用
    static void SendResponseDataList();                                // 主线程调用

private:
    // 文件上传处理
    int _HandleUploadRequest(string &url, string &post_data);
    // 账号注册处理
    int _HandleRegisterRequest(string &url, string &post_data);
    // 账号登录处理
    int _HandleLoginRequest(string &url, string &post_data);
    // 处理文件，共享/删除
    int _HandleDealfileRequest(string &url, string &post_data);
    // 取消分享/转存/更新下载计数
    int _HandleDealsharefileRequest(string &url, string &post_data);
    // 秒传文件处理，上传文件时检查该md5是否已经存在
    int _HandleMd5Request(string &url, string &post_data);
    // 获取个人上传文件
    int _HandleMyfilesRequest(string &url, string &post_data);
    // 获取共享文件或下载榜
    int _HandleSharefilesRequest(string &url, string &post_data);
    // 分享图片
    int _HandleSharepictureRequest(string &url, string &post_data);

protected:
    net_handle_t m_sock_handle;
    uint32_t m_conn_handle;
    bool m_busy;

    uint32_t m_state;
    string m_peer_ip;
    uint16_t m_peer_port;
    CSimpleBuffer m_in_buffer;
    CSimpleBuffer m_out_buffer;

    uint64_t m_last_send_tick;
    uint64_t m_last_recv_tick;

    CHttpParserWrapper m_cHttpParser;

    static uint32_t s_uuid_alloctor; // uuid分配
    uint32_t m_uuid;                 // 自己uuid
    static std::mutex s_resp_mutex;
    static std::list<ResponsePdu_t *> s_response_pdu_list; // 主线程发送回复信息
};

typedef std::unordered_map<uint32_t, CHttpConn *> HttpConnMap_t;

CHttpConn *FindHttpConnByHandle(uint32_t handle);
CHttpConn *FindHttpConnByuuid(uint32_t uuid);
void init_http_conn();