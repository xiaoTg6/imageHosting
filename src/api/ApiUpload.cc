#include "ApiUpload.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#include "fastdfs/fdfs_client.h"
#include "ApiCommon.h"

/* -------------------------------------------*/
/**
 * @brief  将一个本地文件上传到 后台分布式文件系统中
 * 对应 fdfs_upload_file /etc/fdfs/client.conf  完整文件路径
 *
 * @param file_path  (in) 本地文件的路径
 * @param fileid    (out)得到上传之后的文件ID路径
 *
 * @returns
 *      0 succ, -1 fail
 */
/* -------------------------------------------*/
int uploadFileToFastDfs(char *file_path, char *fileid)
{
    int ret = 0;
    pid_t pid;
    int fd[2];

    // 创建无名管道
    if (pipe(fd) < 0) // fd[0] --> r, fd[1] --> w 获取上传后返回的信息 fileid
    {
        LOG_ERROR << "pipe error";
        ret = -1;
        goto END;
    }

    // 创建进程
    pid = fork();
    if (pid < 0)
    {
        LOG_ERROR << "fork error";
        ret = -1;
        goto END;
    }
    else if (pid == 0) // 子进程
    {
        // 关闭读端
        close(fd[0]);

        // 将标准输出重定向到写管道
        dup2(fd[1], STDOUT_FILENO); // 往标准输出写的东西都会重定向到fd所指向的文件, 当fileid产生时输出到管道fd[1]

        // fdfs_upload_file /etc/fdfs/client.conf 123.txt
        // 通过execlp执行fdfs_upload_file  如果函数调用成功,进程自己的执行代码就会变成加载程序的代码,execlp()后边的代码也就不会执行了.
        execlp("fdfs_upload_file", "fdfs_upload_file", s_dfs_path_client.c_str(), file_path, NULL);

        LOG_ERROR << "execlp fdfs_upload_file failed";
        close(fd[1]); // 如果execlp成功调用，紫铜会执行新程序fdfs_upload_file,此程序结束会释放所有文件描述符，包括fd[1]
    }
    else // 父进程
    {
        // 关闭写端
        close(fd[1]);

        // 从管道读数据
        read(fd[0], fileid, TEMP_BUF_MAX_LEN); // 等待管道写入然后读取

        LOG_INFO << "fileid1: " << fileid;
        // 去掉第一个字符串左右的空白字符串
        TrimSpace(fileid);

        if (strlen(fileid) == 0)
        {
            LOG_ERROR << "upload failed";
            ret = -1;
            goto END;
        }
        LOG_INFO << "fileid1: " << fileid;

        wait(NULL);
        close(fd[0]);
    }

END:
    return ret;
}

/* -------------------------------------------*/
/**
 * @brief  封装文件存储在分布式系统中的 完整 url
 *
 * @param fileid        (in)    文件分布式id路径
 * @param fdfs_file_url (out)   文件的完整url地址
 *
 * @returns
 *      0 succ, -1 fail
 */
/* -------------------------------------------*/
int getFullurlByFileid(char *fileid, char *fdfs_file_url)
{
    int ret = 0;

    char *p = NULL;
    char *q = NULL;
    char *k = NULL;

    char fdfs_file_stat_buf[TEMP_BUF_MAX_LEN] = {0};
    char fdfs_file_host_name[HOST_NAME_LEN] = {0}; // storage 所在服务器ip

    pid_t pid;
    int fd[2];

    // 创建无名管道
    if (pipe(fd) < 0)
    {
        LOG_ERROR << "pipe error";
        ret = -1;
        goto END;
    }
    // 创建子进程
    pid = fork();
    if (pid < 0)
    {
        LOG_ERROR << "fork error";
        ret = -1;
        goto END;
    }
    else if (pid == 0) // 子进程
    {
        // 关闭读端
        close(fd[0]);

        dup2(fd[1], STDOUT_FILENO); //==>dup2(fd[1],1)

        execlp("fdfs_file_info", "fdfs_file_info", s_dfs_path_client.c_str(), fileid, NULL);

        // 执行失败
        LOG_ERROR << "execlp fdfs_upload_info failed";
        close(fd[1]);
    }
    else // 父进程
    {
        // 关闭写端
        close(fd[1]);

        read(fd[0], fdfs_file_stat_buf, TEMP_BUF_MAX_LEN);

        wait(NULL);
        close(fd[0]);

        LOG_INFO << "fdfs_file_stat_buf: " << fdfs_file_stat_buf;
        // 拼接上传文件的完整url地址--->http://host_name/group1/M00/00/00/*********.jpg
        p = strstr(fdfs_file_stat_buf, "source ip address: ");

        q = p + strlen("source ip address: ");
        k = strstr(q, "\n");

        strncpy(fdfs_file_host_name, q, k - q);
        fdfs_file_host_name[k - q] = '\0';

        LOG_INFO << "host_name: " << s_storage_web_server_ip << ", fdfs_file_host_name: " << fdfs_file_host_name;

        // storage_web_server服务器的端口

        strcat(fdfs_file_url, "http://");
        strcat(fdfs_file_url, s_storage_web_server_ip.c_str());
        strcat(fdfs_file_url, ":");
        strcat(fdfs_file_url, s_storage_web_server_port.c_str());
        strcat(fdfs_file_url, "/");
        strcat(fdfs_file_url, fileid);

        LOG_INFO << "fdfs_file_url: " << fdfs_file_url;
    }
END:
    return ret;
}

// 将文件信息存入数据库
int storeFileInfo(CDBConn *pDBConn, CacheConn *pCacheConn, char *user, char *filename, char *md5, long size, char *fileid, char *fdfs_file_url)
{
    int ret = 0;
    time_t now;
    char create_time[TIME_STRING_LEN];
    char suffix[SUFFIX_LEN];
    char sql_cmd[SQL_MAX_LEN] = {0};

    // 得到文件后缀字符串，保存到suffix,如果非法文件后缀，返回null
    GetFileSuffix(filename, suffix);
    // sql 语句
    /*
       -- =============================================== 文件信息表
       -- md5 文件md5
       -- file_id 文件id
       -- url 文件url
       -- size 文件大小, 以字节为单位
       -- type 文件类型： png, zip, mp4……
       -- count 文件引用计数， 默认为1， 每增加一个用户拥有此文件，此计数器+1
       */
    sprintf(sql_cmd, "insert into file_info (md5, file_id, url, size, type, count) values ('%s', '%s', '%s', '%ld', '%s', %d)",
            md5, fileid, fdfs_file_url, size, suffix, 1);
    LOG_INFO << "执行: " << sql_cmd;
    if (!pDBConn->ExecuteCreate(sql_cmd))
    {
        LOG_ERROR << "sql_cmd: " << sql_cmd;
        ret = -1;
        goto END;
    }
    // 获取当前时间
    now = time(NULL);
    strftime(create_time, TIME_STRING_LEN - 1, "%Y-%m-%d %H-%M-%S", localtime(&now));
    /*
       -- =============================================== 用户文件列表
       -- user 文件所属用户
       -- md5 文件md5
       -- create_time 文件创建时间
       -- file_name 文件名字
       -- shared_status 共享状态, 0为没有共享， 1为共享
       -- pv 文件下载量，默认值为0，下载一次加1
       */
    // sql语句
    sprintf(sql_cmd, "insert into user_file_list (user, md5, create_time, file_name, shared_status, pv) values ('%s', '%s', '%s', '%s', %d, %d)",
            user, md5, create_time, filename, 0, 0);
    if (!pDBConn->ExecuteCreate(sql_cmd))
    {
        LOG_ERROR << "sql_cmd: " << sql_cmd;
        ret = -1;
        goto END;
    }

    // 询问用户文件数量
    if (CacheIncrCount(pCacheConn, string(user)) < 0)
    {
        LOG_ERROR << "CacheIncrCount failed";
    }
END:
    return ret;
}

int ApiUploadInit(char *dfs_path_client, char *web_server_ip, char *web_server_port, char *storage_web_server_ip, char *storage_web_server_port)
{
    s_dfs_path_client = dfs_path_client;
    s_web_server_ip = web_server_ip;
    s_web_server_port = web_server_port;
    s_storage_web_server_ip = storage_web_server_ip;
    s_storage_web_server_port = storage_web_server_port;
    return 0;
}

int ApiUpload(uint32_t conn_uuid, string url, string post_data)
{
    string str_json;
    UNUSED(url);

    char suffix[SUFFIX_LEN] = {0};
    char fileid[TEMP_BUF_MAX_LEN] = {0};    // 文件上传到fastdfs的文件id
    char fdfs_file_url[FILE_URL_LEN] = {0}; // 文件存放在storage的host_name
    int ret = 0;
    char boundary[TEMP_BUF_MAX_LEN] = {0}; // 分界线信息
    char file_name[128] = {0};
    char file_content_type[128] = {0};
    char file_path[128] = {0};
    char new_file_path[128] = {0};
    char file_md5[128] = {0};
    char file_size[32] = {0};
    long long_file_size = 0;
    char user[32] = {0};
    char *begin = (char *)post_data.c_str();
    char *p1, *p2;

    Json::Value value;

    // 获取数据库连接
    CDBManager *pDBManager = CDBManager::getInstance();
    CDBConn *pDBConn = pDBManager->GetCDBConn("tuchuang_slave");
    AUTO_REL_DBCONN(pDBManager, pDBConn);
    CacheManager *pCacheManager = CacheManager::getInstance();
    CacheConn *pCacheConn = pCacheManager->GetCacheConn("token");
    AUTO_REL_CACHECONN(pCacheManager, pCacheConn);

    LOG_INFO << "post_data: " << post_data;

    // 解析boundary
    //  Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryjWE3qXXORSg2hZiB
    // 找到起始位置
    p1 = strstr(begin, "\r\n");
    if (!p1)
    {
        LOG_ERROR << "wrong no boundary";
        ret = -1;
        goto END;
    }
    // 拷贝分界线
    strncpy(boundary, begin, p1 - begin); // 缓存分解线
    boundary[p1 - begin] = '\0';
    LOG_INFO << "boundary: " << boundary;

    // 查找文件名file_name
    begin = p1 + 2;
    p2 = strstr(begin, "name=\"file_name\"");
    if (!p2)
    {
        LOG_ERROR << "wrong no file name";
        ret = -1;
        goto END;
    }
    p2 = strstr(begin, "\r\n");
    p2 += 4;
    begin = p2;
    p2 = strstr(begin, "\r\n");
    strncpy(file_name, begin, p2 - begin);
    LOG_INFO << "file_name: " << file_name;

    // 查找文件file_content_type
    begin = p1 + 2;
    p2 = strstr(begin, "name=\"file_content_type\"");
    if (!p2)
    {
        LOG_ERROR << "wrong no file_content_type";
        ret = -1;
        goto END;
    }
    p2 = strstr(begin, "\r\n");
    p2 += 4;
    begin = p2;
    p2 = strstr(begin, "\r\n");
    strncpy(file_content_type, begin, p2 - begin);
    LOG_INFO << "file_content_type: " << file_content_type;

    // 查找文件路经file_path
    begin = p2 + 2;
    p2 = strstr(begin, "name=\"file_path\"");
    if (!p2)
    {
        LOG_ERROR << "wrong no file_path";
        ret = -1;
        goto END;
    }
    p2 = strstr(p2, "\r\n");
    p2 += 4;
    begin = p2;
    p2 = strstr(p2, "\r\n");
    strncpy(file_path, begin, p2 - begin);
    LOG_INFO << "file_path: " << file_path;

    // 查找文件的file_md5
    begin = p2 + 2;
    p2 = strstr(begin, "name=\"file_md5\"");
    if (!p2)
    {
        LOG_ERROR << "wrong no file_md5";
        ret = -1;
        goto END;
    }
    p2 = strstr(p2, "\r\n");
    p2 += 4;
    begin = p2;
    p2 = strstr(p2, "\r\n");
    strncpy(file_md5, begin, p2 - begin);
    LOG_INFO << "file_md5: " << file_md5;

    // 查找文件的file_size
    begin = p2 + 2;
    p2 = strstr(begin, "name=\"file_size\"");
    if (!p2)
    {
        LOG_ERROR << "wrong no file_size";
        ret = -1;
        goto END;
    }
    p2 = strstr(p2, "\r\n");
    p2 += 4;
    begin = p2;
    p2 = strstr(p2, "\r\n");
    strncpy(file_size, begin, p2 - begin);
    LOG_INFO << "file_size: " << file_size;
    long_file_size = atol(file_size);

    // 查找user
    begin = p2 + 2;
    p2 = strstr(begin, "name=\"user\""); //
    if (!p2)
    {
        LOG_ERROR << "wrong no user";
        ret = -1;
        goto END;
    }
    p2 = strstr(p2, "\r\n");
    p2 += 4;
    begin = p2;
    p2 = strstr(begin, "\r\n");
    strncpy(user, begin, p2 - begin);
    LOG_INFO << "user: " << user;

    // 获取文件名后缀
    GetFileSuffix(file_name, suffix);
    strcat(new_file_path, file_path);
    strcat(new_file_path, ".");
    strcat(new_file_path, suffix);
    // 重命名 修改文件名
    if (rename(file_path, new_file_path) < 0)
    {
        LOG_ERROR << "rename: " << file_path << " to " << new_file_path << " failed!";
        ret = -1;
        goto END;
    }
    //===============> 将该文件存入fastDFS中,并得到文件的file_id <============
    LOG_INFO << "uploadFileToFastDfs, file_name:" << file_name << ", new_file_path:" << new_file_path;
    if (uploadFileToFastDfs(new_file_path, fileid) < 0)
    {
        LOG_ERROR << "uploadFileToFastDfs failed ";
        ret = -1;
        goto END;
    }
    //================> 删除本地临时存放的上传文件 <===============
    LOG_INFO << "unlink: " << new_file_path;
    ret = unlink(new_file_path);
    if (ret != 0)
    {
        LOG_WARN << "unlink: " << new_file_path << " failed";
    }
    //================> 得到文件所存放storage的host_name <=================
    // 拼接完整的http地址
    LOG_INFO << "getFullurlByFileid, fileid: " << fileid;
    if (getFullurlByFileid(fileid, fdfs_file_url) < 0)
    {
        LOG_ERROR << "getFullurlByFileid failed";
        ret = -1;
        goto END;
    }
    //===============> 将该文件的FastDFS相关信息存入mysql中 <======
    LOG_INFO << "storeFileInfo ,url: " << fdfs_file_url;
    if (storeFileInfo(pDBConn, pCacheConn, user, file_name, file_md5, long_file_size, fileid, fdfs_file_url) < 0)
    {
        LOG_ERROR << "storeFilInfo failed";
        ret = -1;
        // 严谨而言，这里需要删除 已经上传的文件
        goto END;
    }
    ret = 0;
    value["code"] = 0;
    str_json = value.toStyledString(); //// json序列化, 直接用writer是紧凑方式，这里toStyledString是格式化更可读方式

END:
    if (ret == -1)
        value["code"] = 1;
    str_json = value.toStyledString();

    char *str_content = new char[HTTP_RESPONSE_HTML_MAX];
    size_t nlen = str_json.length();
    snprintf(str_content, HTTP_RESPONSE_HTML_MAX, HTTP_RESPONSE_HTML, nlen, str_json.c_str());
    LOG_INFO << "str_content: " << str_content;
    CHttpConn::AddResponseData(conn_uuid, string(str_content));
    delete[] str_content;

    return 0;
}
