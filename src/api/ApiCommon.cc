#include "ApiCommon.h"

string s_dfs_path_client;
string s_web_server_ip;
string s_web_server_port;
string s_storage_web_server_ip;
string s_storage_web_server_port;
string s_shorturl_server_address;
string s_shorturl_server_access_token;

int ApiInit()
{
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pCDBConn = pDBManager->GetCDBConn("tuchuang_slave");

    if (!pCDBConn)
    {
        LOG_ERROR << "GetCDBConn(tuchuang_slave) failed";
        return -1;
    }
    AUTO_REL_DBCONN(pDBManager, pCDBConn);

    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");

    if (!pCacheConn)
    {
        LOG_ERROR << "GetCacheConn(token) failed";
    }
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    int ret = 0;
    int count = 0; // 共享文件的数量，统计加载到redis
    ret = DBGetShareFileCount(pCDBConn, count);
    if (ret < 0)
    {
        LOG_ERROR << "GetShareFileCount failed";
        return -1;
    }
    // 加载到redis
    ret = CacheSetCount(pCacheConn, FILE_PUBLIC_COUNT, (int64_t)count);
    if (ret < 0)
    {
        LOG_ERROR << "CacheSetCount failed";
        return -1;
    }

    return 0;
}

int CacheSetCount(CacheConn *pCacheConn, string key, int64_t count)
{
    string ret = pCacheConn->set(key, std::to_string(count));
    if (!ret.empty())
    {
        return 0;
    }
    else
    {
        return -1;
    }
}
int CacheGetCount(CacheConn *pCacheConn, string key, int64_t &count)
{
    count = 0;
    string str_count = pCacheConn->get(key);
    if (!str_count.empty())
    {
        count = atoll(str_count.c_str());
        return 0;
    }
    else
    {
        return -1;
    }
}
int CacheIncrCount(CacheConn *pCacheConn, string key)
{
    int64_t count = 0;
    int ret = pCacheConn->incr(key, count);
    if (ret < 0)
    {
        return -1;
    }
    LOG_INFO << key << " - " << count;
    return 0;
}
int CacheDecrCount(CacheConn *pCacheConn, string key)
{
    int64_t count = 0;
    int ret = pCacheConn->decr(key, count);
    if (ret < 0)
    {
        return -1;
    }
    LOG_INFO << key << " - " << count;
    if (count < 0)
    {
        LOG_ERROR << key << " 请检查逻辑：" << count << " < 0";
        ret = CacheSetCount(pCacheConn, key, 0); // to ask?
        if (ret < 0)
            return -1;
    }
    return 0;
}

// 获取用户文件个数
int DBGetUserFilesCountByUsername(CDBConn *pCDBConn, string user_name, int &count)
{
    count = 0;
    int ret = 0;
    string str_sql;

    str_sql = formatString("select count(*) from user_file_list where user='%s'", user_name.c_str());
    LOG_INFO << "执行：" << str_sql;
    CResultSet *pResultset = pCDBConn->ExecuteQuery(str_sql.c_str());
    if (pResultset && pResultset->Next()) // 初始时候游标在第一行之前，所以需要next
    {
        count = pResultset->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete pResultset;
    }
    else if (!pResultset)
    {
        LOG_ERROR << str_sql << " 操作失败";
        ret = -1;
    }
    else
    {
        ret = 0;
        LOG_INFO << "没有记录: count: " << count;
    }
    return ret;
}

// 获取用户共享图片个数
int DBGetSharePictureCountByUsername(CDBConn *pCDBConn, string user_name, int &count)
{
    count = 0;
    int ret = 0;
    // 先查看用户是否存在
    string str_sql;

    str_sql = formatString("select count(*) from share_picture_list where user='%s'", user_name.c_str());
    LOG_INFO << "执行: " << str_sql;
    CResultSet *pResultSet = pCDBConn->ExecuteQuery(str_sql.c_str());
    if (pResultSet && pResultSet->Next())
    {
        // 存在在返回
        count = pResultSet->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete pResultSet;
    }
    else if (!pResultSet)
    { // 操作失败
        LOG_ERROR << str_sql << " 操作失败";
        ret = -1;
    }
    else
    {
        // 没有记录则初始化记录数量为0
        ret = 0;
        LOG_INFO << "没有记录: count: " << count;
    }
    return ret;
}

// 获取共享文件的数量
int DBGetShareFileCount(CDBConn *pCDBConn, int &count)
{
    count = 0;
    int ret = 0;
    // 先查看用户是否存在
    string str_sql = "select count(*) from share_picture_list";
    CResultSet *pResultSet = pCDBConn->ExecuteQuery(str_sql.c_str());
    if (pResultSet && pResultSet->Next())
    {
        // 存在在返回
        count = pResultSet->GetInt("count(*)");
        LOG_INFO << "count: " << count;
        ret = 0;
        delete pResultSet;
    }
    else if (!pResultSet)
    { // 操作失败
        LOG_ERROR << str_sql << " 操作失败";
        ret = -1;
    }
    else
    {
        // 没有记录则初始化记录数量为0
        ret = 0;
        LOG_INFO << "没有记录: count: " << count;
    }
    return ret;
}

// 从storage删除指定文件
int RemoveFileFromFastDfs(const char *fileid)
{
    int ret = 0;
    char cmd[1024 * 2] = {0};
    sprintf(cmd, "fdfs_delete_file %s %s", s_dfs_path_client.c_str(), fileid);

    ret = system(cmd);
    LOG_INFO << "RemoveFileFromFastDfs= " << ret;

    return ret;
}