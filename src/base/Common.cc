#include "Common.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctype.h>
#include <memory>

#include "Logging.h"

/**
 * @brief  去掉一个字符串两边的空白字符
 *
 * @param inbuf确保inbuf可修改
 *
 * @returns
 *      0 成功
 *      -1 失败
 */
int TrimSpace(char *inbuf)
{
    if (inbuf == NULL)
    {
        return -1;
    }
    int i = 0;
    int j = strlen(inbuf) - 1;
    char *str = inbuf;
    int count = 0;

    while (isspace(str[i]) && str[i] != '\0')
    {
        i++;
    }
    while (isspace(str[j]) && j > i)
    {
        j--;
    }
    count = j - i + 1;
    strncpy(inbuf, str + i, count);
    inbuf[count] = '\0';
    return 0;
}

/**
 * @brief  解析url query 类似 abc=123&bbb=456 字符串
 *          传入一个key,得到相应的value
 * @returns
 *          0 成功, -1 失败
 */
int QueryParseKeyValue(const char *query, const char *key, char *value, int *value_len_p)
{
    char *temp = NULL;
    char *end = NULL;
    int value_len = 0;

    // 找到是否有key
    temp = (char *)strstr(query, key);
    if (temp == NULL)
    {
        return -1;
    }
    temp += strlen(key); //=
    temp++;              // value
    // get value
    end = temp;
    while ('\0' != *end && '#' != *end && '&' != *end)
    {
        end++;
    }
    value_len = end - temp;
    strncpy(value, temp, value_len);
    value[value_len] = '\0';
    if (value_len_p != NULL)
    {
        *value_len_p = value_len;
    }
    return 0;
}

// 通过文件名file_name， 得到文件后缀字符串, 保存在suffix 如果非法文件后缀,返回"null"
int GetFileSuffix(const char *file_name, char *suffix)
{
    const char *p = file_name;
    int len = 0;
    const char *q = NULL;
    const char *k = NULL;

    if (p == NULL)
    {
        return -1;
    }
    q = p;
    // mike.doc.png            ↑
    while (*q != '\0')
    {
        q++;
    }
    k = q;
    while (*k != '.' && k != p)
    {
        k--;
    }
    if (*k == '.')
    {
        k++;
        len = q - k;

        if (len != 0)
        {
            strncpy(suffix, k, len);
            suffix[len] = '\0';
        }
        else
        {
            strncpy(suffix, "null", 5);
        }
    }
    else
    {
        strncpy(suffix, "null", 5);
    }
    return 0;
}
// 验证登录token，成功返回0，失败返回-1
int VerifyToken(string user_name, string token)
{
    int ret = 0;
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    if (pCacheConn)
    {
        std::string tmp_token = pCacheConn->get(user_name); // 调用redis的get key
        if (tmp_token == token)
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
    return ret;
}

string RandomString(const int len)
{
    std::string str;
    char c;
    int idx;
    for (idx = 0; idx < len; idx++)
    {
        c = 'a' + rand() % 26;
        str.push_back(c);
    }
    return str;
}

// 处理数据库查询结果，结果集保存在buf，只处理一条记录，一个字段，如果buf是null，无需保存结果集，只做判断有没有此记录
// 返回值： 0成功并保存记录集，1没有记录集，2有记录集但是没有保存，-1失败
int GetResultOneCount(CDBConn *pDBConn, char *sql_cmd, int &count)
{
    int ret = -1;
    CResultSet *pResultset = pDBConn->ExecuteQuery(sql_cmd);
    if (!pResultset)
    {
        LOG_ERROR << "!pResultSet faled";
        return ret;
    }
    if (count == 0)
    {
        if (pResultset->Next())
        {
            ret = 0;
            count = pResultset->GetInt("count");
            LOG_DEBUG << "count: " << count;
        }
        else
        {
            ret = 1;
        }
    }
    else
    {
        if (pResultset->Next())
        {
            ret = 2;
        }
        else
        {
            ret = 1; // 没有记录
        }
    }
    delete pResultset;
    return ret;
}
int GetResultOneStatus(CDBConn *pDBConn, char *sql_cmd, int &shared_status)
{
    int ret = 0;
    CResultSet *pResultset = pDBConn->ExecuteQuery(sql_cmd);
    if (!pResultset)
    {
        LOG_ERROR << "!pResultSet faled";
        return -1;
    }
    if (pResultset->Next())
    {
        ret = 0;
        shared_status = pResultset->GetInt("shared_status");
        LOG_INFO << "shared_status: " << shared_status;
    }
    else
    {
        LOG_ERROR << "pResultSet->Next()";
        ret = -1;
    }
    delete pResultset;

    return ret;
}

// 检测是否存在记录，-1 操作失败，0 没有记录，1 有记录
int CheckWhetherHaveRecord(CDBConn *pDBConn, char *sql_cmd)
{
    int ret = 0;
    CResultSet *pResultset = pDBConn->ExecuteQuery(sql_cmd);
    if (!pResultset)
    {
        LOG_ERROR << "!pResultSet faled";
        return -1;
    }
    else if (pResultset && pResultset->Next())
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    delete pResultset;

    return ret;
}
