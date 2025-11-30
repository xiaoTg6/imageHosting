#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <sys/time.h>

#include "ApiMd5.h"
#include "ApiCommon.h"
#include "DBPool.h"

enum Md5State
{
    Md5OK = 0,
    Md5Failed = 1,
    Md5TokenFailed = 4,
    Md5FileExit = 5,
};

int decodeMd5Json(string &str_json, string &user_name, string &token, string &md5, string &filename)
{
    bool res;
    Json::Value root;
    Json::Reader jsonReader;

    res = jsonReader.parse(str_json, root);
    if (!res)
    {
        LOG_ERROR << "parse md5 json failed";
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

    if (root["md5"].isNull())
    {
        LOG_ERROR << "md5 null";
        return -1;
    }
    md5 = root["md5"].asString();

    if (root["filename"].isNull())
    {
        LOG_ERROR << "filename null";
        return -1;
    }
    filename = root["filename"].asString();

    return 0;
}

int encodeMd5Json(int ret, string &str_json)
{
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter jsonWriter;
    str_json = jsonWriter.write(root);
    return 0;
}

// 秒传处理
void handleDealMd5(const char *user, const char *md5, const char *filename, string &str_json)
{
    Md5State md5_state = Md5Failed;
    int ret = 0;
    int file_ref_count = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};

    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    // sql语句，获取此md5值文件的文件计数器count
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5);
    LOG_INFO << "执行: " << sql_cmd;
    // 返回值：0成功并保存记录，1没有记录，2有记录但没保存，-1失败
    file_ref_count = 0;
    ret = GetResultOneCount(pDBConn, sql_cmd, file_ref_count);
    LOG_INFO << "ret: " << ret << ", file_ref_count: " << file_ref_count;
    if (ret == 0) // 有结果，并且返回file_info被引用的计数file_ref_count
    {
        // 查看此用户是否已经由此文件，存在说明已上传无需上传
        sprintf(sql_cmd, "select * from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user, md5, filename);
        LOG_INFO << "执行: " << sql_cmd;
        ret = CheckWhetherHaveRecord(pDBConn, sql_cmd);
        if (ret == 1)
        {
            LOG_WARN << "user: " << user << ", filename: " << filename << ", md5: " << md5 << " 已存在";
            md5_state = Md5FileExit;
            goto END;
        }

        // 修改file_info中的count字段，+1（这是该文件被引用的次数），多了一个用户引用
        sprintf(sql_cmd, "update file_info set count = '%d' where md5 = '%s'", file_ref_count + 1, md5);
        LOG_INFO << "执行: " << sql_cmd;
        if (!pDBConn->ExecuteUpdate(sql_cmd))
        {
            LOG_ERROR << "update count failed";
            md5_state = Md5Failed; // 更新失败也认为是秒传失败
            goto END;
        }

        // 向user_file_list插入该文件记录
        // 当前时间戳
        struct timeval tv;
        struct tm *ptm;
        char time_str[128];

        // 获取时间
        gettimeofday(&tv, NULL);
        ptm = localtime(&tv.tv_sec); // 将时间戳转化为当前本地时间
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", ptm);

        // 用户列表创建记录
        sprintf(sql_cmd, "insert into user_file_list (user, md5, create_time, file_name, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)", user, md5, time_str, filename, 0, 0);
        LOG_INFO << "执行: " << sql_cmd;
        if (!pDBConn->ExecuteCreate(sql_cmd))
        {
            LOG_ERROR << "update count failed";
            md5_state = Md5Failed; // 更新失败也认为是秒传失败
            sprintf(sql_cmd, "update file_info set count = '%d' where md5 = '%s'", file_ref_count, md5);
            LOG_INFO << "执行: " << sql_cmd;
            if (!pDBConn->ExecutePassQuery(sql_cmd))
            {
                LOG_ERROR << "withdraw count failed";
            }
            goto END;
        }

        // 查询用户文件数量，用户数量加一
        if (CacheIncrCount(pCacheConn, FILE_USER_COUNT + string(user)) < 0)
        {
            LOG_WARN << "CacheIncrCount failed";
        }
        md5_state = Md5OK;
    }
    else
    {
        LOG_INFO << "秒传失败";
        md5_state = Md5Failed;
        goto END;
    }
END:
    /*
    秒传文件：
        秒传成功：  {"code": 0}
        秒传失败：  {"code":1}
        文件已存在：{"code": 5}
    */
    int code = (int)md5_state;
    encodeMd5Json(code, str_json);
}

int ApiMd5(uint32_t conn_uuid, string url, string post_data)
{
    string str_json;
    UNUSED(url);
    // 解析json中信息
    /*
        * {
        user:xxxx,
        token: xxxx,
        md5:xxx,
        fileName: xxx
        }
        */
    string user;
    string md5;
    string token;
    string filename;
    int ret = 0;
    ret = decodeMd5Json(post_data, user, token, md5, filename);
    if (ret < 0)
    {
        LOG_ERROR << "decodeMd5Json failed";
        encodeMd5Json((int)Md5Failed, str_json);
        ret = -1;
        goto END;
    }

    // 验证登录token
    ret = VerifyToken(user, token);
    if (ret == 0)
    {
        handleDealMd5(user.c_str(), md5.c_str(), filename.c_str(), str_json);
    }
    else
    {
        LOG_ERROR << "VerifyToken failed";
        encodeMd5Json((int)Md5TokenFailed, str_json);
        ret = -1;
        goto END;
    }
    ret = 0;
END:
    char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
    size_t nlen = str_json.length();
    snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, nlen, str_json.c_str());
    LOG_INFO << "str_content: " << str_content;
    CHttpConn::AddResponseData(conn_uuid, string(str_content));
    delete[] str_content;

    return 0;
}