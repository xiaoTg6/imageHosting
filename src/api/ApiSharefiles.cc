#include "ApiSharefiles.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/time.h>

#include "ApiCommon.h"
#include "redis_keys.h"

// 获取共享文件数量
int getShareFileCount(CDBConn *pDBConn, CacheConn *pCacheConn, int &count)
{
    int ret = 0;
    int64_t file_count = 0;

    // 先从redis里面读
    if (CacheGetCount(pCacheConn, FILE_PUBLIC_COUNT, file_count) < 0)
    {
        LOG_WARN << "CacheGetCount FILE_PUBLIC_COUNT failed";
        ret = -1;
    }

    if (file_count == 0)
    {
        // 从mysql加载
        if (DBGetShareFileCount(pDBConn, count) < 0)
        {
            LOG_ERROR << "DBGetShareFileCount failed";
            return -1;
        }
        file_count = count;
        if (CacheSetCount(pCacheConn, FILE_PUBLIC_COUNT, file_count) < 0)
        {
            LOG_ERROR << "CacheSetCount failed";
            return -1;
        }
        ret = 0;
    }
    count = file_count;
    return ret;
}

// 获取共享文件数量
int handleGetShareFileCount(int &count)
{
    CDBManager *pCDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pCDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pCDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    int ret = getShareFileCount(pDBConn, pCacheConn, count);
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
int decodeShareFileslistJson(string &str_json, int &start, int &count)
{
    bool res;
    Json::Value root;
    Json::Reader jsonReader;
    res = jsonReader.parse(str_json, root);
    if (!res)
    {
        LOG_ERROR << "Sharefilelist json parse failed";
        return -1;
    }

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

// 获取共享文件列表
// 获取用户文件信息 127.0.0.1:80/api/sharefiles&cmd=normal
void handleGetShareFileslist(int start, int count, string &str_json)
{
    int ret = 0;
    string str_sql;
    int total;
    CDBManager *pCDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pCDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pCDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    CResultSet *pResultSet = NULL;
    int file_count = 0;
    Json::Value root, files;

    ret = getShareFileCount(pDBConn, pCacheConn, total);
    if (ret < 0)
    {
        LOG_ERROR << "getShareFileCount failed";
        ret = -1;
        goto END;
    }
    else
    {
        if (total == 0)
        {
            ret = 0;
            goto END;
        }
    }

    str_sql = formatString("select share_file_list.*, file_info.url, file_info.size, file_info.type from file_info, \
        share_file_list where file_info.md5 = share_file_list.md5 limit %d, %d",
                           start, count);
    LOG_DEBUG << "执行: " << str_sql;
    pResultSet = pDBConn->ExecuteQuery(str_sql.c_str());
    if (pResultSet)
    {
        // 遍历所有的查询结果
        file_count = 0;
        while (pResultSet->Next())
        {
            Json::Value file;
            file["user"] = pResultSet->GetString("user");
            file["md5"] = pResultSet->GetString("md5");
            file["file_name"] = pResultSet->GetString("file_name");
            file["share_status"] = pResultSet->GetInt("shared_status"); // 这一行无关紧要，share_file_list中根本就没有又共享状态这栏，只在file_info表里有记录
            file["pv"] = pResultSet->GetInt("pv");
            file["create_time"] = pResultSet->GetString("create_time");
            file["url"] = pResultSet->GetString("url");
            file["size"] = pResultSet->GetInt("size");
            file["type"] = pResultSet->GetString("type");
            files[file_count] = file;
            file_count++;
        }
        if (file_count > 0)
        {
            root["files"] = files;
        }
        ret = 0;
        delete pResultSet;
    }
    else
    {
        ret = -1;
    }
END:
    if (ret == 0)
    {
        root["code"] = 0;
        root["total"] = total;
        root["count"] = file_count;
    }
    else
    {
        root["code"] = 1;
    }
    str_json = root.toStyledString();
}

// 获取共享文件排行榜
// 按下载量降序127.0.0.1:80/api/sharefiles?cmd=pvdesc
void handleGetRankingFilelist(int start, int count, string &str_json)
{
    /*
    a) mysql共享文件数量和redis共享文件数量对比，判断是否相等
    b) 如果不相等，清空redis数据，从mysql中导入数据到redis (mysql和redis交互)
    c) 从redis读取数据，给前端反馈相应信息
    */
    int ret = 0;
    char sql_cmd[SQL_MAX_LEN] = {0};
    int total = 0;
    char filename[1024] = {0};
    int sql_num;
    int redis_num;
    int score;
    int end;
    RVALUES value = NULL; // to ask?
    Json::Value root;
    Json::Value files;
    int file_count = 0;

    CDBManager *pCDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pCDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pCDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    CResultSet *pResultSet = NULL;

    // 获取共享文件的总数量
    ret = getShareFileCount(pDBConn, pCacheConn, total);
    if (ret != 0)
    {
        LOG_ERROR << "getShareFileCount failed";
        ret = -1;
        goto END;
    }
    // 1.mysql共享文件数量
    sql_num = total;

    // 2.redis共享文件数量
    redis_num = pCacheConn->ZsetZcard(FILE_PUBLIC_ZSET); // Zcard用于计算集合中元素数量
    if (redis_num == -1)
    {
        LOG_ERROR << "ZsetZcard failed";
        ret = -1;
        goto END;
    }

    LOG_DEBUG << "sql_num: " << sql_num << ", redis_num: " << redis_num;

    // 3.将mysql和redis文件数量和redis共享文件数量对比判断是否相等
    if (redis_num != sql_num)
    {
        // 清空redis,从mysql向redis导入数据
        pCacheConn->del(FILE_PUBLIC_ZSET); // 删除集合
        pCacheConn->del(FILE_NAME_HASH);   // to ask?
        // 从mysql导入
        // sql语句
        strcpy(sql_cmd, "select md5, file_name, pv from share_file_list order by pv desc");
        LOG_DEBUG << "执行: " << sql_cmd;

        pResultSet = pDBConn->ExecuteQuery(sql_cmd);
        if (!pResultSet)
        {
            LOG_ERROR << sql_cmd << "执行失败";
            ret = -1;
            goto END;
        }

        // mysql_fetch_row 从使用mysql_store_result得到结果结构提取一行，并把它放在行结构中
        // 当数据用完或发生错误时返回null
        while (pResultSet->Next()) // 这里如果文件数量特别多，导致耗时严重，可以设置当mysql记录和redis不一致的时候安排后台线程取执行任务
        {
            char filed[1024] = {0};
            string md5 = pResultSet->GetString("md5");
            string file_name = pResultSet->GetString("file_name");
            int pv = pResultSet->GetInt("pv");
            sprintf(filed, "%s%s", md5.c_str(), file_name.c_str()); // 文件表示：md5+文件名

            // 增加有序集合成员
            pCacheConn->ZsetAdd(FILE_PUBLIC_ZSET, pv, filed);

            // 增加哈希记录
            pCacheConn->hset(FILE_NAME_HASH, filed, file_name);
        }
    }
    // 4.从redis读出数据，给前端返回信息
    // char value[count][VALUES_ID_SIZE];
    value = (RVALUES)calloc(count, VALUES_ID_SIZE); // 堆区请求空间
    if (value == NULL)
    {
        ret = -1;
        goto END;
    }

    file_count = 0;
    end = start + count - 1; // 加载资源的结束位置
    // 降序获取有序集合 file_count获取实际返回个数
    ret = pCacheConn->ZsetZrevrange(FILE_PUBLIC_ZSET, start, end, value, file_count);
    if (ret != 0)
    {
        LOG_ERROR << "ZsetZrevrange failed";
        ret = -1;
        goto END;
    }

    // 遍历元素个数
    for (int i = 0; i < file_count; i++)
    {
        // files[i]
        Json::Value file;
        /*
        {
            "filename": "test.mp4",
            "pv": 0
        }
        */
        ret = pCacheConn->hget(FILE_NAME_HASH, value[i], filename);
        if (ret != 0)
        {
            LOG_ERROR << "hget failed";
            ret = -1;
            goto END;
        }
        file["filename"] = filename;

        // 文件下载量pv
        score = pCacheConn->ZsetGetScore(FILE_PUBLIC_ZSET, value[i]);
        if (score == -1)
        {
            LOG_ERROR << "ZsetGetScore failed";
            ret = -1;
            goto END;
        }
        file["pv"] = score;
        files[i] = file;
    }
    if (file_count > 0)
    {
        root["files"] = files;
    }
END:
    if (ret == 0)
    {
        root["code"] = 0;
        root["total"] = total;
        root["count"] = file_count;
    }
    else
    {
        root["code"] = 1;
    }
    str_json = root.toStyledString();
}

int encodeSharefilesJson(int ret, int total, string &str_json)
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

int ApiSharefiles(string &url, string &post_data, string &str_json)
{
    // 解析url有没有命令
    // count 获取用户文件个数
    // display获取用户文件信息，展示到前端
    char cmd[20];
    string user_name;
    string token;
    int start = 0; // 文件起点
    int count = 0; // 文件个数

    LOG_INFO << "post_data: " << post_data.c_str();

    // 解析命令 解析url获取自定义参数
    QueryParseKeyValue(url.c_str(), "cmd", cmd, NULL);
    LOG_INFO << "cmd = " << cmd;

    if (strcmp(cmd, "count") == 0)
    {
        // 解析json
        if (handleGetShareFileCount(count) < 0)
        {
            encodeSharefilesJson(1, 0, str_json);
        }
        else
        {
            encodeSharefilesJson(0, count, str_json);
        }
        return 0;
    }
    else
    {
        if (decodeShareFileslistJson(post_data, start, count) < 0)
        {
            encodeSharefilesJson(1, 0, str_json);
            return 0;
        }
        if (strcmp(cmd, "normal") == 0)
        {
            handleGetShareFileslist(start, count, str_json);
        }
        else if (strcmp(cmd, "pvdesc") == 0)
        {
            handleGetRankingFilelist(start, count, str_json);
        }
        else
        {
            encodeSharefilesJson(1, 0, str_json);
        }
    }
    return 0;
}