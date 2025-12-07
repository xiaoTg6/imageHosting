#include "HttpConn.h"
#include "ConfigFileReader.h"
#include "CachePool.h"
#include "DBPool.h"
#include "ApiCommon.h"
#include "ApiUpload.h"
#include "ApiDealfile.h"
#include "Logging.h"
#include "util.h"
#include "AsyncLogging.h"
#include "TimeZone.h"

off_t kRollSize = 1 * 1000 * 1000; // 只设置1M
static AsyncLogging *g_asyncLog = NULL;

static void asyncOutput(const char *msg, int len)
{
    g_asyncLog->append(msg, len);
}

int initLog()
{
    printf("pid = %d\n", getpid());
    g_logLevel = Logger::INFO;
    char name[256] = "tuchuang";
    // 回滚大小kRollSize（1M）, 最大1秒刷一次盘（flush）
    AsyncLogging *log = new AsyncLogging(::basename(name), kRollSize, 1); // 注意，每个文件的大小 还取决于时间的问题，不是到了大小就一定换文件。
    Logger::setOutput(asyncOutput);                                       // 不是说只有一个实例

    TimeZone beijing(8 * 3600, "CST");
    Logger::setTimeZone(beijing);
    g_asyncLog = log;
    log->start(); // 启动日志写入线程

    return 0;
}

void deinit()
{
    if (g_asyncLog)
    {
        delete g_asyncLog;
        g_asyncLog = NULL;
    }
}

void http_callback(void *callbcak_data, uint8_t msg, uint32_t handle, void *pParam)
{
    UNUSED(callbcak_data);
    UNUSED(pParam);

    if (msg == NETLIB_MSG_CONNECT)
    {
        CHttpConn *conn = new CHttpConn();
        conn->OnConnect(handle);
    }
    else
    {
        LOG_ERROR << "!!!error msg: " << msg;
    }
}

void http_loop_callback(void *callbcak_data, uint8_t msg, uint32_t handle, void *pParam)
{
    UNUSED(callbcak_data);
    UNUSED(msg);
    UNUSED(handle);
    UNUSED(pParam);
    CHttpConn::SendResponseDataList();
}

int InitHttpConn(int thread_num)
{
    g_thread_pool.init(thread_num);
    g_thread_pool.start();
    netlib_add_loop(http_loop_callback, nullptr);
    return 0;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    UNUSED(argc);
    UNUSED(argv);

    // 初始化日志
    initLog();

    // 初始化mysql，redis连接池
    CDBManager *pDBManager = CDBManager::getInstance(); // 不需要调用init，get内部调用了init
    if (pDBManager == NULL)
    {
        LOG_ERROR << "DBManager Init failed";
        return -1;
    }
    CacheManager *pCacheManager = CacheManager::getInstance();
    if (pCacheManager == NULL)
    {
        LOG_ERROR << "CacheManager Init failed";
        return -1;
    }

    // 读取配置文件
    CConfigFileReader config_file("tc_http_server.conf");

    // 短链主要是将图片链接转成短链
    char *str_enable_shorturl = config_file.GetConfigName("enable_shorturl");
    uint16_t enable_shorturl = atoi(str_enable_shorturl);                                 // 1开启短链，0不开启短链
    char *shorturl_server_address = config_file.GetConfigName("shorturl_server_address"); // 短链服务地址  "127.0.0.1:50051"
    char *shorturl_server_access_token = config_file.GetConfigName("shorturl_server_access_token");

    char *http_listen_ip = config_file.GetConfigName("HttpListenIP");
    char *str_http_port = config_file.GetConfigName("HttpPort");

    char *dfs_path_client = config_file.GetConfigName("dfs_path_client");
    char *web_server_ip = config_file.GetConfigName("web_server_ip");
    char *web_server_port = config_file.GetConfigName("web_server_port");
    char *storage_web_server_ip = config_file.GetConfigName("storage_web_server_ip");
    char *storage_web_server_port = config_file.GetConfigName("storage_web_server_port");

    char *str_thread_num = config_file.GetConfigName("ThreadNum");
    uint32_t thread_num = atoi(str_thread_num);

    // 将配置文件参数传递给对应模块
    if (enable_shorturl == 1)
    {
        ApiUploadInit(dfs_path_client, web_server_ip, web_server_port, storage_web_server_ip, storage_web_server_port, shorturl_server_address, shorturl_server_access_token);
    }
    else
    {
        ApiUploadInit(dfs_path_client, web_server_ip, web_server_port, storage_web_server_ip, storage_web_server_port, "", "");
    }
    ApiDealfileInit(dfs_path_client);
    if (ApiInit() < 0)
    {
        LOG_ERROR << "ApiInit failed";
        return -1;
    }

    // 检测监听ip和端口

    if (!http_listen_ip || !str_http_port)
    {
        LOG_ERROR << "config item missing, exit... ip: " << http_listen_ip << ", port: " << str_http_port;
        return -1;
    }
    uint16_t http_port = atoi(str_http_port);

    int ret = netlib_init();
    if (ret == NETLIB_ERROR)
    {
        LOG_ERROR << "netlib_init failed";
        return -1;
    }
    CStrExplode http_listen_ip_list(http_listen_ip, ';');
    for (uint32_t i = 0; i < http_listen_ip_list.GetItemCnt(); i++)
    {
        ret = netlib_listen(http_listen_ip_list.GetItem(i), http_port, http_callback, NULL);
        if (ret == NETLIB_ERROR)
        {
            return ret;
        }
    }
    InitHttpConn(thread_num);

    LOG_INFO << "server start listen on:For http://" << http_listen_ip << ":" << http_port;
    LOG_INFO << "now enter the event loop...";
    writePid();

    netlib_eventloop(1);
    deinit();
    return 0;
}