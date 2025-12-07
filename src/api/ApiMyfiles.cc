#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/time.h>
#include "ApiMyfiles.h"
#include "json/json.h"
#include "Logging.h"

// 解析json包，登录token
int decodeCountJson(string &str_json, string &user_name, string &token)
{
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res)
    {
        LOG_ERROR << "parse json failed";
        return -1;
    }
    int ret = 0;
    if (root["user"].isNull())
    {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    if (root["token"].isNull())
    {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    return ret;
}

int encodeCountJson(int ret, int total, string &str_json)
{
    Json::Value root;
    root["code"] = ret;
    if (ret == 0)
    {
        root["total"] = total;
    }
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

// 获取用户文件数量
int getUserFilesCount(CDBConn *pDBConn, CacheConn *pCacheConn, string &user_name, int &count)
{
    int64_t file_count = 0;
    LOG_DEBUG << "CacheGetCount begin";

    // 先从redis里面查询，如果数量为0则从mysql查询确定是否为0
    if (CacheGetCount(pCacheConn, FILE_USER_COUNT + user_name, file_count) < 0)
    {
        LOG_WARN << "CacheGetCount failed";
        file_count = 0;
    }
    LOG_DEBUG << "CacheGetCount success";
    if (file_count == 0)
    {
        count = 0;
        if (DBGetUserFilesCountByUsername(pDBConn, user_name, count) < 0)
        {
            LOG_ERROR << "DBGetUserFilesCountByUsername failed";
            return -1;
        }
        file_count = (int64_t)count;
        if (CacheSetCount(pCacheConn, FILE_USER_COUNT + user_name, file_count) < 0)
        {
            LOG_ERROR << "CacheSetCount failed";
            return -1;
        }
    }
    count = file_count;
    return 0;
}

int handleUserFilesCount(string &user_name, int &count)
{
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    int ret = getUserFilesCount(pDBConn, pCacheConn, user_name, count);
    return ret;
}

// 解析的json包
//  参数
//  {
//  "count": 2,
//  "start": 0,
//  "token": "3a58ca22317e637797f8bcad5c047446",
//  "user": "qingfu"
//  }
int decodeFileslistJson(string &str_json, string &user_name, string &token, int &start, int &count)
{
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res)
    {
        LOG_ERROR << "parse json failed";
        return -1;
    }

    if (root["user"].isNull())
    {
        LOG_ERROR << "user null";
        return -1;
    }
    user_name = root["user"].asString();

    if (root["token"].isNull())
    {
        LOG_ERROR << "token null";
        return -1;
    }
    token = root["token"].asString();

    if (root["start"].isNull())
    {
        LOG_ERROR << "start null";
        return -1;
    }
    start = root["start"].asInt();

    if (root["count"].isNull())
    {
        LOG_ERROR << "count null";
        return -1;
    }
    count = root["count"].asInt();

    return 0;
}

// 获取文件列表
int getUserFileList(string &cmd, string &user_name, int &start, int &count, string &str_json)
{
    LOG_INFO << "getUserFileList info";
    int ret = 0;
    int total = 0;
    string str_sql;
    LOG_DEBUG << "pDBConn on";
    CDBManager *pDBManager = CDBManager::getInstance();
    if (!pDBManager)
    {
        LOG_ERROR << "pDBManager is null";
        return -1;
    }
    CDBConn *pDBConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);
    if (!pDBConn)
    {
        LOG_ERROR << "pDBConn is null";
        return -1;
    }
    LOG_DEBUG << "pDBConn on";

    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);
    if (!pCacheConn)
    {
        LOG_ERROR << "pCacheConn is null";
        return -1;
    }
    LOG_DEBUG << "pCacheConn on";
    ret = getUserFilesCount(pDBConn, pCacheConn, user_name, total);
    if (ret < 0)
    {
        LOG_ERROR << "getUserFilesCount failed";
        return -1;
    }
    else
    {
        if (total == 0)
        {
            Json::Value root;
            root["code"] = 0;
            root["count"] = 0;
            root["total"] = 0;
            Json::FastWriter writer;
            str_json = writer.write(root);
            LOG_WARN << "getUserFileCount = 0";
            return 0;
        }
    }

    // 多表指定行查询范围
    if (cmd == "normal") // 获取用户文件信息
    {
        str_sql = formatString("select user_file_list.*, file_info.url, file_info.size,  file_info.type from file_info, user_file_list where user = '%s' \
            and file_info.md5 = user_file_list.md5 limit %d, %d",
                               user_name.c_str(), start, count);
    }
    else if (cmd == "pvasc") // 按下载量升序
    {
        // sql语句
        str_sql = formatString("select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, \
         user_file_list where user = '%s' and file_info.md5 = user_file_list.md5  order by pv asc limit %d, %d",
                               user_name.c_str(), start, count);
    }
    else if (cmd == "pvdesc") // 按照下载量降序
    {
        // sql语句
        str_sql = formatString("select user_file_list.*, file_info.url, file_info.size, file_info.type from file_info, \
         user_file_list where user = '%s' and file_info.md5 = user_file_list.md5  order by pv desc limit %d, %d",
                               user_name.c_str(), start, count);
    }
    else
    {
        LOG_ERROR << "unknown cmd: " << cmd;
        return -1;
    }

    LOG_DEBUG << "执行: " << str_sql;
    CResultSet *pResultSet = pDBConn->ExecuteQuery(str_sql.c_str());
    if (pResultSet)
    {
        // 遍历所有内容
        // 获取大小
        int file_index = 0;
        Json::Value root, files;
        root["code"] = 0;
        while (pResultSet->Next())
        {
            Json::Value file;
            file["user"] = pResultSet->GetString("user");
            file["md5"] = pResultSet->GetString("md5");
            file["create_time"] = pResultSet->GetString("create_time");
            file["file_name"] = pResultSet->GetString("file_name");
            file["share_status"] = pResultSet->GetInt("shared_status");
            file["pv"] = pResultSet->GetInt("pv");
            file["url"] = pResultSet->GetString("url");
            file["size"] = pResultSet->GetInt("size");
            file["type"] = pResultSet->GetString("type");
            files[file_index] = file;
            file_index++;
        }
        root["files"] = files;
        root["count"] = file_index;
        root["total"] = total;

        Json::FastWriter writer;
        str_json = writer.write(root);
        delete pResultSet;
        return 0;
    }
    else
    {
        LOG_ERROR << str_sql << " 操作失败";
        return -1;
    }
}

int ApiMyfiles(uint32_t conn_uuid, string url, string post_data)
{
    // 解析url有没有命令

    // count 获取用户文件个数
    // display 获取用户文件信息，展示在前端
    string str_json;
    char cmd[20];
    string user_name;
    string token;
    int ret = 0;
    int start = 0; // 文件起点
    int count = 0; // 文件个数

    // 解析命令 解析url获取自定义参数
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LOG_INFO << "url: " << url << ", cmd: " << cmd;

    if (strcmp(cmd, "count") == 0)
    {
        // 解析json
        if (decodeCountJson(post_data, user_name, token) < 0)
        {
            encodeCountJson(1, 0, str_json);
            LOG_ERROR << "decodeCountJson failed";
            ret = -1;
            goto END;
        }
        // 验证登陆token，成功返回0，失败-1
        ret = VerifyToken(user_name, token);
        if (ret == 0)
        {
            // 获取文件数量
            if (handleUserFilesCount(user_name, count) < 0)
            {
                LOG_ERROR << "handleUserFilesCount failed";
                encodeCountJson(1, 0, str_json);
                ret = -1;
                goto END;
            }
            else
            {
                LOG_INFO << "handleUserFilesCount ok, count: " << count;
                encodeCountJson(0, count, str_json);
                ret = 0;
                goto END;
            }
        }
        else
        {
            LOG_ERROR << "VerifyToken failed";
            encodeCountJson(1, 0, str_json);
            ret = -1;
            goto END;
        }
    }
    else
    {
        if ((strcmp(cmd, "normal") != 0) && (strcmp(cmd, "pvasc") != 0) && (strcmp(cmd, "pvdesc") != 0))
        {
            LOG_ERROR << "unknown cmd: " << cmd;
            encodeCountJson(1, 0, str_json);
            ret = -1;
            goto END;
        }
        // 获取用户文件信息 127.0.0.1:80/api/myfiles&cmd=normal
        // 按下载量升序 127.0.0.1:80/api/myfiles?cmd=pvasc
        // 按下载量降序127.0.0.1:80/api/myfiles?cmd=pvdesc
        ret = decodeFileslistJson(post_data, user_name, token, start, count); // 通过json包获取信息
        LOG_INFO << "user_name: " << user_name << ", token:" << token << ", start: " << start << ", count:" << count;
        if (ret == 0)
        {
            // 验证登录token，成功返回0，失败-1
            ret = VerifyToken(user_name, token);
            if (ret == 0)
            {
                string str_cmd = cmd;
                if (getUserFileList(str_cmd, user_name, start, count, str_json) < 0)
                {
                    LOG_ERROR << "getUserFileList failed";
                    encodeCountJson(1, 0, str_json);
                    ret = -1;
                    goto END;
                }
            }
            else
            {
                LOG_ERROR << "VerifyToken failed";
                encodeCountJson(1, 0, str_json);
                ret = -1;
                goto END;
            }
        }
        else
        {
            LOG_ERROR << "decodeFileslistJson failed";
            encodeCountJson(1, 0, str_json);
            ret = -1;
            goto END;
        }
    }
END:

    char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
    size_t nlen = str_json.length();
    snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, nlen, str_json.c_str());
    LOG_DEBUG << "str_content: " << str_content;
    CHttpConn::AddResponseData(conn_uuid, string(str_content));
    delete[] str_content;

    return 0;
}