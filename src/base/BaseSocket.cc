#include "BaseSocket.h"
#include "EventDispatch.h"
#include "Logging.h"

#include <unordered_map>

typedef std::unordered_map<net_handle_t, CBaseSocket *> SocketMap;
SocketMap g_socket_map;

void AddBaseSocket(CBaseSocket *pSocket)
{
    g_socket_map.insert(std::make_pair((net_handle_t)pSocket->GetSocket(), pSocket));
}

void RemoveBaseSocket(CBaseSocket *pSocket)
{
    g_socket_map.erase((net_handle_t)pSocket->GetSocket());
}

CBaseSocket *FindBaseSocket(net_handle_t fd)
{
    auto it = g_socket_map.find(fd);
    if (it != g_socket_map.end())
    {
        CBaseSocket *pSocket = it->second;
        pSocket->AddRef();
        return pSocket;
    }
    return NULL;
}

////////////
CBaseSocket::CBaseSocket()
{
    m_socket = C_INVALID_SOCKET;
    m_state = SOCKET_STATE_IDLE;
}
CBaseSocket::~CBaseSocket()
{
}

int CBaseSocket::Listen(const char *server_ip, uint16_t port, callback_t callback, void *callback_data)
{
    m_local_ip = server_ip;
    m_local_port = port;
    m_callback = callback;
    m_callback_data = callback_data;

    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == C_INVALID_SOCKET)
    {
        LOG_ERROR << "socket failed,err_code= " << _GetErrorCode() << "server_ip: " << server_ip
                  << "port: " << port;
        return NETLIB_ERROR;
    }

    _SetNonBlock(m_socket);
    _SetReuseAddr(m_socket);

    sockaddr_in serv_addr;
    _SetAddr(server_ip, port, &serv_addr);
    int ret = ::bind(m_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "bind failed,err_code= " << _GetErrorCode() << "server_ip: " << server_ip
                  << "port: " << port;
        closesocket(m_socket);
        return NETLIB_ERROR;
    }

    ret = listen(m_socket, 64);
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "listen failed,err_code= " << _GetErrorCode() << "server_ip: " << server_ip
                  << "port: " << port;
        closesocket(m_socket);
        return NETLIB_ERROR;
    }

    m_state = SOCKET_STATE_LISTENING;

    LOG_INFO << "CBaseSocket::Listen on " << server_ip << ":" << port;

    AddBaseSocket(this);
    CEventDispatch::Instance()->AddEvent(m_socket, SOCKET_READ | SOCKET_EXCEP);
    return NETLIB_OK;
}

net_handle_t CBaseSocket::Connect(const char *server_ip, uint16_t port, callback_t callback, void *callback_data)
{
    LOG_INFO << "CBaseSocket::Connect, server_ip= " << server_ip << ", port= " << port;

    m_remote_ip = server_ip;
    m_remote_port = port;
    m_callback = callback;
    m_callback_data = callback_data;

    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == C_INVALID_SOCKET)
    {
        LOG_ERROR << "socket failed,err_code= " << _GetErrorCode() << "server_ip: " << server_ip
                  << "port: " << port;
        closesocket(m_socket);
        return NETLIB_INVALID_HANDLE;
    }

    _SetNonBlock(m_socket);
    _SetNoDelay(m_socket);
    struct sockaddr_in serv_addr;
    _SetAddr(server_ip, port, &serv_addr);
    int ret = connect(m_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret == SOCKET_ERROR && !_isBlock(_GetErrorCode()))
    {
        LOG_ERROR << "connect failed,err_code= " << _GetErrorCode() << "server_ip: " << server_ip
                  << "port: " << port;
        closesocket(m_socket);
        return NETLIB_INVALID_HANDLE;
    }

    m_state = SOCKET_STATE_CONNECTING;
    AddBaseSocket(this);
    CEventDispatch::Instance()->AddEvent(m_socket, SOCKET_ALL);
    return (net_handle_t)m_socket;
}

int CBaseSocket::Send(void *buf, int len)
{
    if (m_state != SOCKET_STATE_CONNECTED)
    {
        return NETLIB_ERROR;
    }
    int ret = send(m_socket, buf, len, 0);
    if (ret == SOCKET_ERROR)
    {
        int err_code = _GetErrorCode();
        if (_isBlock(err_code))
        {
            ret = 0;
        }
        else
        {
            LOG_ERROR << "send failed,err_code= " << err_code << ", len= " << len;
        }
    }
    return ret;
}

int CBaseSocket::Recv(void *buf, int len)
{
    return recv(m_socket, buf, len, 0);
}

int CBaseSocket::Close()
{
    CEventDispatch::Instance()->RemoveEvent(m_socket, SOCKET_ALL);
    RemoveBaseSocket(this);
    close(m_socket);
    ReleaseRef();
    return 0;
}

void CBaseSocket::OnRead()
{
    if (m_state == SOCKET_STATE_LISTENING)
    {
        _AcceptNewSocket();
    }
    else
    {
        u_long avail = 0;
        int ret = ioctlsocket(m_socket, FIONREAD, &avail); // avail中存的是当前socket中尚未被读取的字节数
        // 这里没有谁用recv的原因是将真正的读操作交给回调函数，如果这里recv了，回调就读不到了，所以这里只是起到了检测的作用
        if ((SOCKET_ERROR == ret) || (avail == 0))
        {
            m_callback(m_callback_data, NETLIB_MSG_CLOSE, (net_handle_t)m_socket, NULL);
        }
        else
        {
            m_callback(m_callback_data, NETLIB_MSG_READ, (net_handle_t)m_socket, NULL);
        }
    }
}
void CBaseSocket::OnWrite()
{
    if (m_state == SOCKET_STATE_CONNECTING)
    {
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (void *)&error, &len);
        if (error)
        {
            m_callback(m_callback_data, NETLIB_MSG_CLOSE, (net_handle_t)m_socket, NULL);
        }
        else
        {
            m_state = SOCKET_STATE_CONNECTED;
            m_callback(m_callback_data, NETLIB_MSG_WRITE, (net_handle_t)m_socket, NULL);
        }
    }
    else
    {
        m_callback(m_callback_data, NETLIB_MSG_WRITE, (net_handle_t)m_socket, NULL);
    }
}
void CBaseSocket::OnClose()
{
    m_state = SOCKET_STATE_CLOSING;
    m_callback(m_callback_data, NETLIB_MSG_CLOSE, (net_handle_t)m_socket, NULL);
}

void CBaseSocket::SetSendBufSize(uint32_t send_size)
{
    int ret = setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &send_size, 4);
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "set SO_SNDBUF failed for fd= " << m_socket;
    }

    socklen_t len = 4;
    int size = 0;
    getsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &size, &len);
    LOG_INFO << "socket= " << m_socket << " recv_buf_size= " << size;
}
void CBaseSocket::SetRecvBufSize(uint32_t recv_size)
{
    int ret = setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &recv_size, 4);
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "set SO_RCVBUF failed for fd= " << m_socket;
    }

    socklen_t len = 4;
    int size = 0;
    getsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &size, &len);
    LOG_INFO << "socket= " << m_socket << " recv_buf_size= " << size;
}

int CBaseSocket::_GetErrorCode()
{
    return errno;
}
bool CBaseSocket::_isBlock(int error_code)
{
    return ((error_code == EINPROGRESS) || (error_code == EWOULDBLOCK));
}

void CBaseSocket::_SetNonBlock(SOCKET fd)
{
    int ret = fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "_SetNonBlock failed,err_code= " << _GetErrorCode() << " fd= " << fd;
    }
}
void CBaseSocket::_SetReuseAddr(SOCKET fd)
{
    int reuse = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "_SetReuseAddr failed,err_code= " << _GetErrorCode() << " fd= " << fd;
    }
}
void CBaseSocket::_SetNoDelay(SOCKET fd)
{
    int nodelay = 1;
    int ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, sizeof(nodelay));
    if (ret == SOCKET_ERROR)
    {
        LOG_ERROR << "_SETNoDelay failed,err_code= " << _GetErrorCode() << " fd= " << fd;
    }
}
void CBaseSocket::_SetAddr(const char *ip, const uint16_t port, sockaddr_in *pAddr)
{
    memset(pAddr, 0, sizeof(sockaddr_in));
    pAddr->sin_family = AF_INET;
    pAddr->sin_addr.s_addr = inet_addr(ip);
    pAddr->sin_port = htons(port);
    if (pAddr->sin_addr.s_addr == INADDR_NONE)
    {
        hostent *host = gethostbyname(ip);
        if (!host)
        {
            LOG_ERROR << "gethostbyname failed, ip=" << ip << ", port=" << port;
            return;
        }

        pAddr->sin_addr.s_addr = *(uint32_t *)host->h_addr;
    }
}

void CBaseSocket::_AcceptNewSocket()
{
    SOCKET fd = 0;
    sockaddr_in peer_addr;
    socklen_t addr_len = sizeof(peer_addr);
    char ip_str[64] = {0};
    while ((fd = accept(m_socket, (struct sockaddr *)&peer_addr, &addr_len)) != C_INVALID_SOCKET)
    {
        CBaseSocket *pSocket = new CBaseSocket();
        uint32_t ip = ntohl(peer_addr.sin_addr.s_addr);
        uint16_t port = ntohs(peer_addr.sin_port);

        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);

        LOG_INFO << "AcceptNewSocket, socket= " << fd << "from " << ip_str << ":" << port;
        pSocket->SetSocket(fd);
        pSocket->SetCallback(m_callback);
        pSocket->SetCallbackData(m_callback_data);
        pSocket->SetState(SOCKET_STATE_CONNECTED);
        pSocket->SetRemoteIP(ip_str);
        pSocket->SetRemotePort(port);

        _SetNonBlock(fd);
        _SetNoDelay(fd);
        AddBaseSocket(pSocket);
        CEventDispatch::Instance()->AddEvent(fd, SOCKET_READ | SOCKET_EXCEP);
        m_callback(m_callback_data, NETLIB_MSG_CONNECT, (net_handle_t)fd, NULL);
    }
}