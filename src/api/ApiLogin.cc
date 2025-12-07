#include "ApiLogin.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>

#define LOGIN_RET_OK 0   // 成功
#define LOGIN_RET_FAIL 1 // 失败

// 解析登录信息：反序列化
int decodeLoginJson(const std::string &str_json, string &user_name, string &pwd)
{
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res)
    {
        LOG_ERROR << "parse login json failed";
        return -1;
    }
    // 用户名
    if (root["user"].isNull())
    {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    // 密码
    if (root["pwd"].isNull())
    {
        LOG_ERROR << "pwd null";
        return -1;
    }
    pwd = root["pwd"].asString();

    return 0;
}

// 封装登录结果的json：序列化
int encodeLoginJson(int ret, string &token, string &str_json)
{
    Json::Value root;
    root["code"] = ret;
    if (ret == 0)
    {
        root["token"] = token;
    }
    Json::FastWriter jsonWriter;
    str_json = jsonWriter.write(root);
    return 0;
}

// 判断用户登录情况
int verifyUserPassword(string &user_name, string &pwd)
{
    int ret = 0;
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pConn);

    // 查看用户是否存在
    string strSql;
    strSql = formatString("select password from user_info where user_name='%s'", user_name.c_str());
    CResultSet *pResultSet = pConn->ExecuteQuery(strSql.c_str());
    if (pResultSet && pResultSet->Next())
    {
        string password = pResultSet->GetString("password");
        LOG_INFO << "mysql-pwd: " << password << ", user-pwd: " << pwd;
        if (password == pwd)
        {
            ret = 0;
        }
        else
        {
            ret = -1;
        }
    }
    else
    {
        ret = -1;
    }
    delete pResultSet;
    return ret;
}

// 生成token字符串，保存redis数据库
int setToken(string &user_name, string &token)
{
    int ret = 0;
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    token = RandomString(32);
    if (pCacheConn)
    {
        // 用户名，超时时间，token
        pCacheConn->setex(user_name, 86400, token); // redis做超时
    }
    else
    {
        ret = -1;
    }
    return ret;
}

// 从mysql加载文件数量和共享文件数量到redis
int loadMyfilesCountAndSharepictureCount(string &user_name)
{
    int64_t redis_file_count = 0;
    int mysq_file_count = 0;

    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    // 从mysql中加载
    if (DBGetUserFilesCountByUsername(pDBConn, user_name, mysq_file_count) < 0)
    {
        LOG_ERROR << "DBGetUserFilesCountByUsername failed";
        return -1;
    }
    redis_file_count = (int64_t)mysq_file_count;
    if (CacheSetCount(pCacheConn, FILE_USER_COUNT + user_name, redis_file_count) < 0)
    {
        LOG_ERROR << "CacheSetCount failed";
        return -1;
    }
    LOG_INFO << "FILE_USER_COUNT: " << redis_file_count;

    // 从mysql加载
    if (DBGetSharePictureCountByUsername(pDBConn, user_name, mysq_file_count) < 0)
    {
        LOG_ERROR << "DBGetSharePictureCountByUsername failed";
        return -1;
    }
    redis_file_count = (int64_t)mysq_file_count;
    if (CacheSetCount(pCacheConn, SHARE_PIC_COUNT + user_name, redis_file_count) < 0)
    {
        LOG_ERROR << "CacheSetCount failed";
        return -1;
    }
    LOG_INFO << "SHARE_PICTURE_COUNT: " << redis_file_count;

    return 0;
}

int ApiUserLogin(uint32_t conn_uuid, string url, string post_data)
{
    string str_json;
    UNUSED(url);
    int ret = 0;
    string user_name;
    string pwd;
    string token;

    LOG_INFO << "uuid: " << conn_uuid << ", url: " << url << ", post_data: " << post_data;
    // 判断数据是否为空
    if (post_data.empty())
    {
        LOG_ERROR << "post_data null";
        ret = -1;
        goto END;
    }
    // 解析json
    if (decodeLoginJson(post_data, user_name, pwd) < 0)
    {
        LOG_ERROR << "decodeLoginJson failed";
        encodeLoginJson(1, token, str_json);
        ret = -1;
        goto END;
    }

    // 验证账号和密码是否匹配
    if (verifyUserPassword(user_name, pwd) < 0)
    {
        LOG_ERROR << "verifyUserPassword failed";
        encodeLoginJson(1, token, str_json);
        ret = -1;
        goto END;
    }

    // 生成token
    if (setToken(user_name, token) < 0)
    {
        LOG_ERROR << "setToken failed";
        encodeLoginJson(1, token, str_json);
        ret = -1;
        goto END;
    }

    // 加载 我的文件数量 我的分享图片数量
    if (loadMyfilesCountAndSharepictureCount(user_name) < 0)
    {
        LOG_ERROR << "loadMyfilesCountAndSharepictureCount failed";
        encodeLoginJson(1, token, str_json);
        ret = -1;
        goto END;
    }
    encodeLoginJson(0, token, str_json);
END:
    char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
    size_t nlen = str_json.length();
    snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, nlen, str_json.c_str());
    LOG_DEBUG << "str_content: " << str_content;
    CHttpConn::AddResponseData(conn_uuid, string(str_content));
    delete[] str_content;

    return ret;
}