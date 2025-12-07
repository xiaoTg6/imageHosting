#pragma once

#include <string>
#include "redis_keys.h"
#include "Common.h"
#include "json/json.h"
#include "DBPool.h"
#include "CachePool.h"
#include "Logging.h"
#include "HttpConn.h"

#define API_REGISTER_MUTI_THREAD 1
#define API_LOGIN_MUTI_THREAD 1
#define API_MYFILES_MUTI_THREAD 1

using std::string;
extern string s_dfs_path_client;
extern string s_web_server_ip;
extern string s_web_server_port;
extern string s_storage_web_server_ip;
extern string s_storage_web_server_port;
extern string s_shorturl_server_address;
extern string s_shorturl_server_access_token;

int ApiInit();

int CacheSetCount(CacheConn *pCacheConn, string key, int64_t count);
int CacheGetCount(CacheConn *pCacheConn, string key, int64_t &count);
int CacheIncrCount(CacheConn *pCacheConn, string key);
int CacheDecrCount(CacheConn *pCacheConn, string key);
int DBGetUserFilesCountByUsername(CDBConn *pCDBConn, string user_name, int &count);
int DBGetShareFileCount(CDBConn *pCDBConn, int &count);
int DBGetSharePictureCountByUsername(CDBConn *pCDBConn, string user_name, int &count);
int RemoveFileFromFastDfs(const char *fileid);