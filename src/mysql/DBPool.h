#pragma once

#include <iostream>
#include <list>
#include <mutex>
#include <condition_variable>
#include <map>
#include <stdint.h>

#include <mysql/mysql.h>

#define MAX_ESCAPE_STRING_LEN 10240

// 返回结果select的时候使用
class CResultSet
{
public:
    CResultSet(MYSQL_RES *res);
    virtual ~CResultSet();

    bool Next();
    int GetInt(const char *key);
    char *GetString(const char *key);

private:
    int _GetIndex(const char *key);

    MYSQL_RES *m_res; // 该结构代表返回行的查询结果（SELECT, SHOW, DECRIBE, EXPLAIN
    MYSQL_ROW m_row;  // 这是一行数据的类型安全表示，它目前是按照计数字节字符串的数组实施的（二维数组）
    std::map<std::string, int> m_key_map;
};

// 插入数据时候使用(预处理)
class CPrepareStatement
{
public:
    CPrepareStatement();
    virtual ~CPrepareStatement();

    bool Init(MYSQL *mysql, std::string &sql);

    void SetParam(uint32_t index, int &value);
    void SetParam(uint32_t index, uint32_t &value);
    void SetParam(uint32_t index, std::string &value);
    void SetParam(uint32_t index, const std::string &value);

    bool ExecuteUpdate();
    uint32_t GetInsertId();

private:
    MYSQL_STMT *m_stmt;
    MYSQL_BIND *m_param_bind;
    uint32_t m_param_cnt;
};

class CDBPool;

class CDBConn
{
public:
    CDBConn(CDBPool *pPool);
    virtual ~CDBConn();
    int Init();

    // 创建表
    bool ExecuteCreate(const char *sql_query);
    // 删除表
    bool ExecuteDrop(const char *sql_query);
    // 查询
    CResultSet *ExecuteQuery(const char *sql_query);

    bool ExecutePassQuery(const char *sql_query);
    /**
     *  执行DB更新，修改
     *
     *  @param sql_query     sql
     *  @param care_affected_rows  是否在意影响的行数，false:不在意；true:在意
     *
     *  @return 成功返回true 失败返回false
     */
    bool ExecuteUpdate(const char *sql_query, bool care_affected_rows = true);
    uint32_t GetInsertId();

    // 开启事务
    bool StartTrasaction();
    // 提交事务
    bool Commit();
    // 回滚事务
    bool Rollback();
    // 获取连接池名
    const char *GetPoolName();
    MYSQL *GetMysql() { return m_mysql; }
    int GetRowNum() { return row_num; }

private:
    int row_num = 0;
    CDBPool *m_pDBpool;
    MYSQL *m_mysql;
    char m_escape_string[MAX_ESCAPE_STRING_LEN + 1];
};

class CDBPool
{
public:
    CDBPool();
    CDBPool(const char *pool_name, const char *db_server_ip, uint16_t db_server_port,
            const char *username, const char *password, const char *db_name,
            int max_conn_cnt);
    virtual ~CDBPool();

    int Init();                                   // 连接数据库，创建连接
    CDBConn *GetDBConn(const int timeout_ms = 0); // 获取连接资源
    void RelDBConn(CDBConn *pConn);               // 归还连接资源

    const char *GetPoolName() { return m_pool_name.c_str(); }
    const char *GetDBServerIP() { return m_db_server_ip.c_str(); }
    uint16_t GetDBServerPort() { return m_db_server_port; }
    const char *GetUsername() { return m_username.c_str(); }
    const char *GetPasswrod() { return m_password.c_str(); }
    const char *GetDBName() { return m_db_name.c_str(); }

private:
    std::string m_pool_name;    // 连接池名称
    std::string m_db_server_ip; // 数据库ip
    uint16_t m_db_server_port;  // 数据库端口
    std::string m_username;     // 用户名
    std::string m_password;     // 用户密码
    std::string m_db_name;      // db名称
    int m_db_cur_conn_cnt;      // 当前启用的连接数量
    int m_db_max_conn_cnt;      // 最大连接数量

    std::list<CDBConn *> m_free_list; // 空闲的连接
    // std::list<CDBConn *> m_used_list; // 记录已经被请求的连接 todo

    std::mutex m_mutex;
    std::condition_variable m_cond_var; // 条件变量
    bool m_abort_request = false;       // 判断是否终止请求
};

class CDBManager
{
public:
    virtual ~CDBManager();

    static CDBManager *getInstance();

    int Init();

    CDBConn *GetCDBConn(const char *dbpool_name);
    void RelDBConn(CDBConn *pConn);

private:
    CDBManager();

private:
    static CDBManager *s_db_manager;
    std::map<std::string, CDBPool *> m_dbpool_map;
};

class AutoRelDBConn
{
public:
    AutoRelDBConn(CDBManager *manager, CDBConn *conn) : manager_(manager), conn_(conn) {}
    ~AutoRelDBConn()
    {
        if (manager_)
        {
            manager_->RelDBConn(conn_);
        }
    }

private:
    CDBManager *manager_ = NULL;
    CDBConn *conn_ = NULL;
};

#define AUTO_REL_DBCONN(m, c) AutoRelDBConn autoreldbconn(m, c)