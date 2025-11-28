#include "ApiDealfile.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include "redis_keys.h"
#include "ApiCommon.h"

enum ShareState
{
    ShareOK = 0,        // 分享成功
    ShareFail = 1,      // 分享失败
    ShareHad = 3,       // 别人已经分享此文件
    ShareTokenFail = 4, // token验证失败
};

int decodeDealfileJson(string &str_json, string &user_name, string &token, string &md5, string &filename)
{
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res)
    {
        LOG_ERROR << "parse reg json failed ";
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

int encodeDealfileJson(int ret, string &str_json)
{
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);

    LOG_DEBUG << "str_json: " << str_json;
    return 0;
}

// 分享文件
int handleShareFile(string &user, string &md5, string &filename)
{
    /*
    a)先判断此文件是否已经分享，判断集合有没有这个文件，如果有，说明别人已经分享此文件，中断操作(redis操作)
    b)如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
    c)如果mysql有记录，而redis没有记录，说明redis没有保存此文件，redis保存此文件信息后，再中断操作(redis操作)
    d)如果此文件没有被分享，mysql保存一份持久化操作(mysql操作)
    e)redis集合中增加一个元素(redis操作)
    f)redis对应的hash也需要变化 (redis操作)
    */
    ShareState share_state = ShareFail;
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};
    int ret2 = 0;

    CDBManager *pCDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pCDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pCDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    // 文件标识，md5+文件名
    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());

    ret2 = pCacheConn->ZsetExit(FILE_PUBLIC_ZSET, fileid);
    if (ret2 == 1) // 存在
    {
        LOG_WARN << "别人已经分享此文件";
        share_state = ShareHad;
        goto END;
    }
    else if (ret2 == 0) // 不存在
    {
        //===2、如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
        //===3、如果mysql有记录，而redis没有记录，说明redis没有保存此文件，redis保存此文件信息后，再中断操作(redis操作)
        // 查看此文件别人是否已经分享了
        sprintf(sql_cmd, "select * from share_file_list where md5 = '%s' and file_name = '%s'", md5.c_str(), filename.c_str());
        LOG_DEBUG << "执行: " << sql_cmd;
        ret2 = CheckWhetherHaveRecord(pDBConn, sql_cmd);
        // 1表示有记录
        if (ret2 == 1)
        {
            // redis保存此文件信息
            pCacheConn->ZsetAdd(FILE_PUBLIC_ZSET, 0, fileid);
            pCacheConn->hset(FILE_NAME_HASH, fileid, filename);
            LOG_WARN << "别人已经分享此文件";
            share_state = ShareHad;
            goto END;
        }
    }
    else // 出错
    {
        ret = -1;
        goto END;
    }

    //===4、如果此文件没有被分享，mysql保存一份持久化操作(mysql操作)
    // sql语句, 更新共享标志字段
    sprintf(sql_cmd, "update user_file_list set shared_status = 1 where user = '%s' and md5 = '%s' and file_name = '%s'", user.c_str(), md5.c_str(), filename.c_str());
    LOG_DEBUG << "执行: " << sql_cmd;
    if (!pDBConn->ExecuteUpdate(sql_cmd, false))
    {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }

    time_t now;
    char create_time[TIME_STRING_LEN];
    // 获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H:%M:%S", localtime(&now));
    // 分享文件的信息，额外保存在share_file_list保存列表
    /*
        -- user	文件所属用户
        -- md5 文件md5
        -- create_time 文件共享时间
        -- file_name 文件名字
        -- pv 文件下载量，默认值为1，下载一次加1
    */
    sprintf(sql_cmd, "insert into share_file_list (user, md5, create_time, file_name, pv) values ('%s', '%s', '%s', '%s', %d)",
            user.c_str(), md5.c_str(), create_time, filename.c_str(), 0);
    LOG_DEBUG << "执行: " << sql_cmd;
    if (!pDBConn->ExecuteCreate(sql_cmd))
    {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }

    // 共享文件数量+1
    ret = CacheIncrCount(pCacheConn, FILE_PUBLIC_COUNT);
    if (ret < 0)
    {
        LOG_ERROR << "CacheIncrCount failed";
        ret = -1;
        goto END;
    }

    //===5、redis集合中增加一个元素(redis操作)
    pCacheConn->ZsetAdd(FILE_PUBLIC_ZSET, 0, fileid);

    //===6、redis对应的hash也需要变化 (redis操作)
    pCacheConn->hset(FILE_NAME_HASH, fileid, filename);
    share_state = ShareOK;
END:
    return (int)share_state;
}

// 删除文件
int handleDeleteFile(string &user, string &md5, string &filename)
{
    /*
    a)先判断此文件是否已经分享
    b)判断集合有没有这个文件，如果有，说明别人已经分享此文件(redis操作)
    c)如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
    d)如果mysql有记录，而redis没有记录，那么分享文件处理只需要处理mysql (mysql操作)
    e)如果redis有记录，mysql和redis都需要处理，删除相关记录
    */
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char fileid[1024] = {0};
    int ret2 = 0;
    int count = 0;
    int is_shared = 0;        // 共享状态
    int redis_has_record = 0; // 标志redis是否有记录

    CDBManager *pCDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pCDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pCDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    // 文件标识，文件md5+文件名
    sprintf(fileid, "%s%s", md5.c_str(), filename.c_str());

    //===1、先判断此文件是否已经分享，判断集合有没有这个文件，如果有，说明别人已经分享此文件
    ret2 = pCacheConn->ZsetExit(FILE_PUBLIC_ZSET, fileid);
    if (ret2 == 1) // 存在
    {
        is_shared = 1;
        redis_has_record = 1;
    }
    else if (ret2 == 0) // 不存在
    {
        //===2、如果集合没有此元素，可能因为redis中没有记录，再从mysql中查询，如果mysql也没有，说明真没有(mysql操作)
        is_shared = 0;
        // sql
        sprintf(sql_cmd, "select shared_status from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user.c_str(), md5.c_str(), filename.c_str());
        LOG_DEBUG << "执行: " << sql_cmd;
        int shared_status = 0;
        ret2 = GetResultOneStatus(pDBConn, sql_cmd, shared_status);
        if (ret2 == 0)
        {
            LOG_INFO << "GetResultOneCount share  = " << shared_status;
            is_shared = shared_status; // 要从mysql里面获取赋值
        }
    }
    else
    {
        ret = -1;
        goto END;
    }
    LOG_INFO << "is_shared = " << is_shared;
    // 说明此文件被共享，删除分享列表（shared_file_list)的数据
    if (is_shared == 1)
    {
        //===3、如果mysql有记录，删除相关分享记录 (mysql操作)
        // 删除在共享列表的数据，如果自己分享了这个文件，那同时从分享列表种删除
        sprintf(sql_cmd, "delete from share_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user.c_str(), md5.c_str(), filename.c_str());
        LOG_DEBUG << "执行: " << sql_cmd;
        if (!pDBConn->ExecuteDrop(sql_cmd))
        {
            LOG_ERROR << sql_cmd << "操作失败";
            ret = -1;
            goto END;
        }

        // 共享文件数量-1
        // 查询共享文件数量
        if (CacheDecrCount(pCacheConn, FILE_PUBLIC_COUNT) < 0)
        {
            LOG_ERROR << "CacheDecrCount 操作失败";
            ret = -1;
            goto END;
        }

        //===4、如果redis有记录，redis需要处理，删除相关记录
        if (1 == redis_has_record)
        {
            // 有序集合删除指定成员
            pCacheConn->ZsetZrem(FILE_PUBLIC_ZSET, fileid);

            // 从哈希中删除相应记录
            pCacheConn->hdel(FILE_NAME_HASH, fileid);
        }
    }

    // 用户文件数量-1
    if (CacheDecrCount(pCacheConn, FILE_USER_COUNT + user) < 0)
    {
        LOG_ERROR << "CacheDecrCount 操作失败";
        ret = -1;
        goto END;
    }

    // 删除用户文件列表数据
    sprintf(sql_cmd, "delete from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user.c_str(), md5.c_str(), filename.c_str());
    LOG_DEBUG << "执行: " << sql_cmd;
    if (!pDBConn->ExecuteDrop(sql_cmd))
    {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }

    // 文件信息表(file_info)的文件引用计数count，减去1
    // 查看该文件文件引用计数
    sprintf(sql_cmd, "select count from file_info where md5 = '%s'", md5.c_str());
    LOG_DEBUG << "执行: " << sql_cmd;
    count = 0;
    ret2 = GetResultOneCount(pDBConn, sql_cmd, count);
    if (ret2 != 0)
    {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }

    if (count > 0)
    {
        count -= 1;
        sprintf(sql_cmd, "update file_info set count=%d where md5 = '%s'", count, md5.c_str());
        LOG_DEBUG << "执行: " << sql_cmd;
        if (!pDBConn->ExecuteUpdate(sql_cmd))
        {
            LOG_ERROR << sql_cmd << " 操作失败";
            ret = -1;
            goto END;
        }
    }

    if (count == 0) // 说明已经没有用户拥有此文件，需要在storage删除此文件
    {
        // 查询文件id
        sprintf(sql_cmd, "select file_id from file_info where md5 = '%s'", md5.c_str());
        string fileid;
        CResultSet *pResultSet = pDBConn->ExecuteQuery(sql_cmd);
        if (pResultSet && pResultSet->Next())
        {
            fileid = pResultSet->GetString("file_id");
        }
        else
        {
            LOG_ERROR << sql_cmd << " 操作失败";
            ret = -1;
            goto END;
        }
        delete pResultSet;

        // 删除文件信息表中该文件的信息
        sprintf(sql_cmd, "delete from file_info where md5 = '%s'", md5.c_str());
        if (!pDBConn->ExecuteDrop(sql_cmd))
        {
            LOG_WARN << sql_cmd << " 操作失败";
        }

        // 从storage服务器删除此文件，参数为文件id
        ret2 = RemoveFileFromFastDfs(fileid.c_str());
        if (ret2 != 0)
        {
            LOG_ERROR << "RemoveFileFromFastDfs err: " << ret2;
            ret = -1;
            goto END;
        }
    }

    ret = 0;
END:
    /*
        删除文件：
            成功：{"code":"013"}
            失败：{"code":"014"}
        */
    if (ret == 0)
    {
        return HTTP_RESP_OK;
    }
    else
    {
        return HTTP_RESP_FAIL;
    }
}

// 文件下载标志处理
int handlePvFile(string &user, string &md5, string &filename)
{
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    char sql_cmd1[SQL_MAX_LEN] = {0};
    int pv = 0;
    int shared_status = 0;

    CDBManager *pCDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pCDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pCDBManager, pDBConn);

    // 查询该文件的pv字段
    sprintf(sql_cmd, "select pv, shared_status from user_file_list where user = '%s' and md5 = '%s' and file_name = '%s'", user.c_str(), md5.c_str(), filename.c_str());

    LOG_DEBUG << "执行: " << sql_cmd;
    CResultSet *pResultSet = pDBConn->ExecuteQuery(sql_cmd);
    if (pResultSet && pResultSet->Next())
    {
        pv = pResultSet->GetInt("pv");
        shared_status = pResultSet->GetInt("shared_status");
    }
    else
    {
        LOG_ERROR << sql_cmd << " 操作失败";
        ret = -1;
        goto END;
    }

    // 更新该文件pv字段，+1
    // user_file_list表的pv字段+1
    sprintf(sql_cmd, "update user_file_list set pv = %d where user = '%s' and md5 = '%s' and file_name = '%s'", pv + 1, user.c_str(), md5.c_str(), filename.c_str());
    LOG_DEBUG << "执行: " << sql_cmd;
    if (!pDBConn->ExecuteUpdate(sql_cmd))
    {
        LOG_ERROR << sql_cmd << "操作失败";
        ret = -1;
        goto END;
    }
    // 更新该文件pv字段，+1
    // user_file_list表的pv字段+1
    if (shared_status == 1)
    {
        sprintf(sql_cmd1, "update share_file_list set pv = %d where user = '%s' and md5 = '%s' and file_name = '%s'", pv + 1, user.c_str(), md5.c_str(), filename.c_str());
        LOG_DEBUG << "执行: " << sql_cmd1;
        if (!pDBConn->ExecuteUpdate(sql_cmd1))
        {
            LOG_ERROR << sql_cmd1 << "操作失败";
            ret = -1;
            goto END;
        }
    }
END:
    /*
        下载文件pv字段处理
            成功：{"code":0}
            失败：{"code":1}
        */
    if (ret == 0)
    {
        return HTTP_RESP_OK;
    }
    else
    {
        return HTTP_RESP_FAIL;
    }
}

int ApiDealfileInit(char *dfs_path_client)
{
    s_dfs_path_client = dfs_path_client;
    return 0;
}

int ApiDealfile(string &url, string &post_data, string &str_json)
{
    char cmd[20];
    string user_name;
    string token;
    string md5;
    string filename;
    int ret = 0;

    // 解析命令
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LOG_INFO << "cmd: " << cmd;

    ret = decodeDealfileJson(post_data, user_name, token, md5, filename);
    if (ret < 0)
    {
        encodeDealfileJson(ShareFail, str_json);
        return 0;
    }
    LOG_INFO << "user_name:" << user_name << ", token:" << token << ", md5:" << md5 << ", filename:" << filename;
    // 验证登录token，成功返回0，失败返回-1
    ret = VerifyToken(user_name, token);
    if (ret != 0)
    {
        encodeDealfileJson(ShareTokenFail, str_json); // token验证失败错误码
        return 0;
    }

    if (strcmp(cmd, "share") == 0)
    {
        ret = handleShareFile(user_name, md5, filename);
        encodeDealfileJson(ret, str_json);
    }
    else if (strcmp(cmd, "del") == 0)
    {
        ret = handleDeleteFile(user_name, md5, filename);
        encodeDealfileJson(ret, str_json);
    }
    else if (strcmp(cmd, "pv") == 0)
    {
        ret = handlePvFile(user_name, md5, filename);
        encodeDealfileJson(ret, str_json);
    }
    else
    {
        LOG_WARN << "unknown request: " << cmd;
        encodeDealfileJson(1, str_json);
    }

    return 0;
}