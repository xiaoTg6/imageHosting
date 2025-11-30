#include "ApiRegister.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/time.h>

#include "HttpConn.h"
#include "Common.h"
#include "DBPool.h"
#include "Logging.h"
#include "json/json.h"

// 解析用户注册信息的json包
/*json数据如下
    {
        userName:xxxx,
        nickName:xxx,
        firstPwd:xxx,
        phone:xxx,
        email:xxx
    }
    */

// 反序列化：将json转化为结构化对象
int decodeRegisterJson(const string &str_json, string &user_name, string &nick_name, string &pwd, string &phone, string &email)
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

    // 用户名
    if (root["userName"].isNull())
    {
        LOG_ERROR << "userName null";
        return -1;
    }
    user_name = root["userName"].asString();

    // 昵称
    if (root["nickName"].isNull())
    {
        LOG_ERROR << "nickName null";
        return -1;
    }
    nick_name = root["FirstPwd"].asString();
    // 密码
    if (root["FirstPwd"].isNull())
    {
        LOG_ERROR << "FirstPwd null";
        return -1;
    }
    pwd = root["FirstPwd"].asString();
    // 电话
    if (root["phone"].isNull())
    {
        LOG_WARN << "phone null";
    }
    else
    {
        phone = root["phone"].asString();
    }
    // 邮箱
    if (root["email"].isNull())
    {
        LOG_WARN << "email null";
    }
    else
    {
        email = root["email"].asString();
    }

    return 0;
}

// 序列化：把结构化数据转化为可在网络传输的json序列
int encodeRegisterJson(int ret, string &str_json)
{
    Json::Value root;
    root["code"] = ret;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

template <typename... Args>
string formatString2(const string &format, Args... args)
{
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1); // 把'\0'排除在外
}

int registerUser(string &user_name, string &nick_name, string &pwd, string &phone, string &email)
{
    int ret = 0;
    uint32_t user_id;
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);

    string strSql;
    strSql = formatString2("select * from user_info where user_name=`%s`", user_name.c_str());
    CResultSet *pResultSet = pDBConn->ExecuteQuery(strSql.c_str());
    if (pResultSet && pResultSet->Next())
    {
        // 用户存在
        LOG_WARN << "id: " << pResultSet->GetInt("id") << ", user_name: " << pResultSet->GetString("user_name") << "已经存在";
        delete pResultSet;
        ret = 2;
    }
    else
    {
        // 用户不存在，注册
        time_t now;
        char create_time[TIME_STRING_LEN];
        now = time(NULL);
        strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H-%M-%S", localtime(&now));
        strSql = "insert into user_info ('user_name','nick_name','password','phone','email','create_time') value (?,?,?,?,?,?)";
        LOG_INFO << "执行: " << strSql;
        // 必须在释放连接前delete CPrepareStatement对象，否则有可能多个线程操作mysql对象，会crash
        CPrepareStatement *stmt = new CPrepareStatement();
        if (stmt->Init(pDBConn->GetMysql(), strSql))
        {
            uint32_t index = 0;
            string c_time = create_time;
            stmt->SetParam(index++, user_name);
            stmt->SetParam(index++, nick_name);
            stmt->SetParam(index++, pwd);
            stmt->SetParam(index++, phone);
            stmt->SetParam(index++, email);
            stmt->SetParam(index++, c_time);
            bool bRet = stmt->ExecuteUpdate();
            if (bRet)
            {
                ret = 0;
                user_id = pDBConn->GetInsertId();
                LOG_INFO << "insert user" << user_id;
            }
            else
            {
                LOG_ERROR << "insert user_info failed" << strSql;
                ret = 1;
            }
        }
        delete stmt;
    }
    return ret;
}

int ApiRegisterUser(uint32_t conn_uuid, string url, string post_data)
{
    string str_json;
    UNUSED(url);
    int ret = 0;
    string user_name;
    string nick_name;
    string pwd;
    string phone;
    string email;

    // 判断是否为空
    if (post_data.empty())
    {
        ret = -1;
        goto END;
    }
    // 解析json
    if (decodeRegisterJson(post_data, user_name, nick_name, pwd, phone, email) < 0) // 数据在post_data里面
    {
        LOG_ERROR << "decodeRegisterJson failed";
        encodeRegisterJson(1, str_json);
        ret = -1;
        goto END;
    }

    // 注册账号
    ret = registerUser(user_name, nick_name, pwd, phone, email);
    ret = encodeRegisterJson(ret, str_json);

END:
    char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
    size_t nlen = str_json.length();
    snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, nlen, str_json.c_str());
    LOG_INFO << "str_content: " << str_content;
    CHttpConn::AddResponseData(conn_uuid, string(str_content));
    delete[] str_content;

    return ret;
}
