#include "DBPool.h"
#include "Logging.h"
#include "ConfigFileReader.h"

#define MIN_DB_CONN_CNT 1
#define MAX_DB_CONN_FAIL_NUM 10

CDBManager *CDBManager::s_db_manager = NULL;

CResultSet::CResultSet(MYSQL_RES *res)
{
    m_res = res;

    int num_fields = mysql_num_fields(m_res);        // 查询结果总行数
    MYSQL_FIELD *fields = mysql_fetch_fields(m_res); // 所有列的MYSQL_FIELD数组

    for (int i = 0; i < num_fields; i++)
    {
        m_key_map.insert(std::make_pair(fields[i].name, i));
        LOG_DEBUG << num_fields << " - [" << i << "] - " << fields[i].name;
    }
}

CResultSet::~CResultSet()
{
    if (m_res)
    {
        mysql_free_result(m_res);
        m_res = NULL;
    }
}

bool CResultSet::Next()
{
    m_row = mysql_fetch_row(m_res);
    if (m_row)
    {
        return true;
    }
    else
    {
        return false;
    }
}

int CResultSet::_GetIndex(const char *key)
{
    auto it = m_key_map.find(key);
    if (it != m_key_map.end())
    {
        return it->second;
    }
    else
    {
        return -1;
    }
}

int CResultSet::GetInt(const char *key)
{
    int idx = _GetIndex(key);
    if (idx == -1)
    {
        return 0;
    }
    else
    {
        return atoi(m_row[idx]); // 这里需要检查
    }
}
char *CResultSet::GetString(const char *key)
{
    int idx = _GetIndex(key);
    if (idx == -1)
    {
        return 0;
    }
    else
    {
        return m_row[idx];
    }
}

////////////////////
CPrepareStatement::CPrepareStatement()
{
    m_stmt = NULL;
    m_param_bind = NULL;
    m_param_cnt = 0;
}
CPrepareStatement::~CPrepareStatement()
{
    if (m_stmt)
    {
        mysql_stmt_close(m_stmt);
        m_stmt = NULL;
    }
    if (m_param_bind)
    {
        delete[] m_param_bind;
        m_param_bind = NULL;
    }
    m_param_cnt = 0;
}

bool CPrepareStatement::Init(MYSQL *mysql, std::string &sql)
{
    mysql_ping(mysql);

    m_stmt = mysql_stmt_init(mysql);
    if (m_stmt == nullptr)
    {
        LOG_ERROR << "mysql stmt init error";
        return false;
    }

    if (mysql_stmt_prepare(m_stmt, sql.c_str(), sql.size()))
    {
        LOG_ERROR << "mysql_stmt_prepare failed: " << mysql_stmt_error(m_stmt);
        return false;
    }

    m_param_cnt = mysql_stmt_param_count(m_stmt);
    if (m_param_cnt > 0)
    {
        m_param_bind = new MYSQL_BIND[m_param_cnt]; // to ask?
        if (!m_param_bind)
        {
            LOG_ERROR << "new MYSQL_BIND failed";
            return false;
        }
        memset(m_param_bind, 0, sizeof(MYSQL_BIND) * m_param_cnt);
    }
    return true;
}

void CPrepareStatement::SetParam(uint32_t index, int &value)
{
    if (index >= m_param_cnt)
    {
        LOG_ERROR << "index too large" << index;
        return;
    }

    m_param_bind[index].buffer_type = MYSQL_TYPE_LONG;
    m_param_bind[index].buffer = &value;
}
void CPrepareStatement::SetParam(uint32_t index, uint32_t &value)
{
    if (index >= m_param_cnt)
    {
        LOG_ERROR << "index too large" << index;
        return;
    }

    m_param_bind[index].buffer_type = MYSQL_TYPE_LONG;
    m_param_bind[index].buffer = &value;
}
void CPrepareStatement::SetParam(uint32_t index, std::string &value)
{
    if (index >= m_param_cnt)
    {
        LOG_ERROR << "index too large" << index;
        return;
    }

    m_param_bind[index].buffer_type = MYSQL_TYPE_STRING;
    m_param_bind[index].buffer = (char *)value.c_str();
    m_param_bind[index].buffer_length = value.length();
}
void CPrepareStatement::SetParam(uint32_t index, const std::string &value)
{
    if (index >= m_param_cnt)
    {
        LOG_ERROR << "index too large" << index;
        return;
    }

    m_param_bind[index].buffer_type = MYSQL_TYPE_STRING;
    m_param_bind[index].buffer = (char *)value.c_str(); // c_str返回的是指向字符数组的const char*，buffer是void*
    m_param_bind[index].buffer_length = value.length();
}

bool CPrepareStatement::ExecuteUpdate()
{
    if (!m_stmt)
    {
        LOG_ERROR << "no stmt";
        return false;
    }
    if (mysql_stmt_bind_param(m_stmt, m_param_bind))
    {
        LOG_ERROR << "mysql_stmt_bind_param failed" << mysql_stmt_error(m_stmt);
        return false;
    }
    if (mysql_stmt_execute(m_stmt))
    {
        LOG_ERROR << "mysql_stmt_execute failed" << mysql_stmt_error(m_stmt);
        return false;
    }
    if (mysql_stmt_affected_rows(m_stmt) == 0)
    {
        LOG_ERROR << "ExecuteUpdate have no effect";
        return false;
    }
    return true;
}
uint32_t CPrepareStatement::GetInsertId()
{
    return mysql_stmt_insert_id(m_stmt);
}

/////////////////////
CDBConn::CDBConn(CDBPool *pPool)
{
    m_pDBpool = pPool;
    m_mysql = NULL;
}
CDBConn::~CDBConn()
{
    if (m_mysql)
    {
        mysql_close(m_mysql);
    }
}
int CDBConn::Init()
{
    m_mysql = mysql_init(NULL);
    if (!m_mysql)
    {
        LOG_ERROR << " mysql_init failed";
        return 1;
    }

    bool reconnect = true;
    mysql_options(m_mysql, MYSQL_OPT_RECONNECT, &reconnect);   // 配合mysql_ping实现自动重连
    mysql_options(m_mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4"); //

    //
    if (!mysql_real_connect(m_mysql, m_pDBpool->GetDBServerIP(), m_pDBpool->GetUsername(), m_pDBpool->GetPasswrod(),
                            m_pDBpool->GetDBName(), m_pDBpool->GetDBServerPort(), NULL, 0))
    {
        LOG_ERROR << "mysql_real_connect failed: " << mysql_error(m_mysql);
        return 2;
    }

    return 0;
}

// 创建表
bool CDBConn::ExecuteCreate(const char *sql_query)
{
    mysql_ping(m_mysql);
    // mysql_real_query实际就是执行sql语句
    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: " << sql_query;
        return false;
    }

    return true;
}
// 删除表
bool CDBConn::ExecuteDrop(const char *sql_query)
{
    mysql_ping(m_mysql);
    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: " << sql_query;
        return false;
    }

    return true;
}
bool CDBConn::ExecutePassQuery(const char *sql_query)
{
    mysql_ping(m_mysql);
    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: " << sql_query;
        return false;
    }

    return true;
}
// 查询
CResultSet *CDBConn::ExecuteQuery(const char *sql_query)
{
    mysql_ping(m_mysql);
    row_num = 0;
    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: " << sql_query;
        return NULL;
    }
    // 返回结果
    MYSQL_RES *res = mysql_store_result(m_mysql);
    if (!res)
    {
        LOG_ERROR << "mysql_store_result failed" << mysql_error(m_mysql);
        return NULL;
    }
    row_num = mysql_num_rows(res);
    LOG_INFO << "row_num: " << row_num;
    CResultSet *result_set = new CResultSet(res);
    return result_set;
}

/*
1.执行成功，则返回受影响的行的数目，如果最近一次查询失败的话，函数返回 -1

2.对于delete,将返回实际删除的行数.

3.对于update,如果更新的列值原值和新值一样,如update tables set col1=10 where id=1;
id=1该条记录原值就是10的话,则返回0。

mysql_affected_rows返回的是实际更新的行数,而不是匹配到的行数。
*/
bool CDBConn::ExecuteUpdate(const char *sql_query, bool care_affected_rows)
{
    mysql_ping(m_mysql);

    if (mysql_real_query(m_mysql, sql_query, strlen(sql_query)))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: " << sql_query;
        return false;
    }
    if (mysql_affected_rows(m_mysql) > 0)
    {
        return true;
    }
    else
    {
        if (care_affected_rows)
        {
            LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: " << sql_query;
            return false;
        }
        else
        {
            LOG_WARN << "affected rows = 0,sql: " << sql_query;
            return true;
        }
    }
}

uint32_t CDBConn::GetInsertId()
{
    return (uint32_t)mysql_insert_id(m_mysql);
}

// 开启事务
bool CDBConn::StartTrasaction()
{
    mysql_ping(m_mysql);

    if (mysql_query(m_mysql, "START TRANSACTION"))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: start transaction";
        return false;
    }
    return true;
}
// 提交事务
bool CDBConn::Commit()
{
    mysql_ping(m_mysql);

    if (mysql_query(m_mysql, "COMMIT"))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: commit";
        return false;
    }
    return true;
}
// 回滚事务
bool CDBConn::Rollback()
{
    mysql_ping(m_mysql);

    if (mysql_query(m_mysql, "ROLLBACK"))
    {
        LOG_ERROR << "mysql_real_query failed: " << mysql_error(m_mysql) << ", sql: rollback";
        return false;
    }
    return true;
}
// 获取连接池名
const char *CDBConn::GetPoolName()
{
    return m_pDBpool->GetPoolName();
}

/////////////
CDBPool::CDBPool(const char *pool_name, const char *db_server_ip, uint16_t db_server_port,
                 const char *username, const char *password, const char *db_name,
                 int max_conn_cnt)
{
    m_pool_name = pool_name;
    m_db_server_ip = db_server_ip;
    m_db_server_port = db_server_port;
    m_username = username;
    m_password = password;
    m_db_name = db_name;
    m_db_max_conn_cnt = max_conn_cnt;    //
    m_db_cur_conn_cnt = MIN_DB_CONN_CNT; // 最小连接数量
}
CDBPool::~CDBPool()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_abort_request = true;
    m_cond_var.notify_all();

    for (auto it = m_free_list.begin(); it != m_free_list.end(); it++)
    {
        CDBConn *pConn = *it;
        delete pConn;
    }
    m_free_list.clear();
}

int CDBPool::Init()
{
    for (int i = 0; i < m_db_cur_conn_cnt; i++)
    {
        CDBConn *pDBConn = new CDBConn(this);
        int ret = pDBConn->Init();
        if (ret)
        {
            delete pDBConn;
            return ret;
        }

        m_free_list.push_back(pDBConn);
    }
    return 0;
}

/*
 *TODO: 增加保护机制，把分配的连接加入另一个队列，这样获取连接时，如果没有空闲连接，
 *TODO: 检查已经分配的连接多久没有返回，如果超过一定时间，则自动收回连接，放在用户忘了调用释放连接的接口
 * timeout_ms默认为 0死等
 * timeout_ms >0 则为等待的时间
 */
CDBConn *CDBPool::GetDBConn(const int timeout_ms)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_abort_request)
    {
        LOG_WARN << "have abort";
        return NULL;
    }

    if (m_free_list.empty()) // 没有连接可用
    {
        if (m_db_cur_conn_cnt >= m_db_max_conn_cnt)
        {
            //<=0为阻塞式
            if (timeout_ms <= 0)
            {
                m_cond_var.wait(lock, [this]
                                { return (!m_free_list.empty()) || m_abort_request; });
            }
            // 有最大等待时长
            else
            {
                m_cond_var.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]
                                    { return (!m_free_list.empty()) || m_abort_request; });
                // 为空说明等待超时，返回null
                if (m_free_list.empty())
                {
                    return NULL;
                }
            }

            if (m_abort_request)
            {
                LOG_WARN << " have abort";
                return NULL;
            }
        }
        else
        {
            // 此时连接池还未达最大连接数，创造连接
            CDBConn *pDBConn = new CDBConn(this);
            int ret = pDBConn->Init();
            if (ret)
            {
                LOG_ERROR << "Init DBConn failed";
                delete pDBConn;
                return NULL;
            }
            else
            {
                m_free_list.push_back(pDBConn);
                m_db_cur_conn_cnt++;
            }
        }
    }

    CDBConn *pDBConn = m_free_list.front();
    m_free_list.pop_front();

    return pDBConn;
}

void CDBPool::RelDBConn(CDBConn *pConn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_free_list.begin();
    for (; it != m_free_list.end(); it++)
    {
        if (*it == pConn)
            break;
    }
    if (it == m_free_list.end())
    {
        m_free_list.push_back(pConn);
        m_cond_var.notify_all();
    }
    else
    {
        LOG_WARN << "REL DBConn failed";
    }
}

///////////////////
CDBManager::CDBManager() {}
CDBManager::~CDBManager() {}

CDBManager *CDBManager::getInstance()
{
    if (!s_db_manager)
    {
        s_db_manager = new CDBManager();
        if (s_db_manager->Init())
        {
            delete s_db_manager;
            return NULL;
        }
    }
    return s_db_manager;
}

int CDBManager::Init()
{
    CConfigFileReader config_file("tc_http_server.conf");

    char *db_instances = config_file.GetConfigName("DBInstances");

    if (!db_instances)
    {
        LOG_ERROR << "not configure DBInstances";
        return 1;
    }

    char host[64];
    char port[64];
    char dbname[64];
    char username[64];
    char password[64];
    char maxconncnt[64];

    // 分割字符串类，这个类在构造的时候就会把分割后的字符串存在类内成员变量（char **）中，
    CStrExplode instances_name(db_instances, ',');

    for (int i = 0; i < instances_name.GetItemCnt(); i++) // 这里的函数返回的是分割后的字符串个数
    {
        char *pool_name = instances_name.GetItem(i);
        snprintf(host, 64, "%s_host", pool_name);
        snprintf(port, 64, "%s_port", pool_name);
        snprintf(dbname, 64, "%s_dbname", pool_name);
        snprintf(username, 64, "%s_username", pool_name);
        snprintf(password, 64, "%s_password", pool_name);
        snprintf(maxconncnt, 64, "%s_maxconncnt", pool_name);

        char *db_host = config_file.GetConfigName(host);
        char *str_db_port = config_file.GetConfigName(port);
        char *db_dbname = config_file.GetConfigName(dbname);
        char *db_username = config_file.GetConfigName(username);
        char *db_password = config_file.GetConfigName(password);
        char *str_maxconncnt = config_file.GetConfigName(maxconncnt);

        LOG_INFO << "db_host:" << db_host << ", db_port:" << str_db_port << ", db_dbname:" << db_dbname
                 << ", db_username:" << db_username << ", db_password:" << db_password;

        if (!db_host || !str_db_port || !db_dbname || !db_username || !db_password || !str_maxconncnt)
        {
            LOG_FATAL << "not configure db instance: " << pool_name;
            return 2;
        }

        int db_port = atoi(str_db_port);
        int db_maxconncnt = atoi(str_maxconncnt);
        CDBPool *pDBPool = new CDBPool(pool_name, db_host, db_port, db_username, db_password, db_dbname, db_maxconncnt);
        if (pDBPool->Init())
        {
            LOG_ERROR << "init db instance failed" << pool_name;
            return 3;
        }
        m_dbpool_map.insert(make_pair(std::string(pool_name), pDBPool));
    }
    return 0;
}

CDBConn *CDBManager::GetCDBConn(const char *dbpool_name)
{
    auto it = m_dbpool_map.find(dbpool_name);
    if (it == m_dbpool_map.end())
    {
        return NULL;
    }
    else
    {
        return it->second->GetDBConn();
    }
}
void CDBManager::RelDBConn(CDBConn *pConn)
{
    if (!pConn)
    {
        return;
    }
    auto it = m_dbpool_map.find(pConn->GetPoolName());
    if (it != m_dbpool_map.end())
    {
        it->second->RelDBConn(pConn);
    }
}