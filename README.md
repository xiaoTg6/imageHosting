# 1 编译说明

使用短链时需要生成grpc接口
```
cd tc-src/api
protoc --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=/usr/local/bin/grpc_cpp_plugin shorturl.proto

```


```
cd src
mkdir build
cd build
cmake ..
make
```

得到执行文件

```
sudo ./tc_http_server
```
如果之前的配置是root权限，这里启动程序的时候也以root权限启动，这样不会存在权限的问题。（nginx文件上传模块会把文件写到临时目录，这里的服务程序需要进行重命名，需要权限）

**需要将修改的tc_http_server.conf的拷贝到执行目录。**



# 2 导入数据库

```
root@iZbp1h2l856zgoegc8rvnhZ:~/0voice/tc/tuchuang$ mysql -uroot -p          #登录mysql

mysql>
mysql> source /root/tuchuang/tuchuang/tuchuang.sql;   #导入带索引的数据库，具体看自己存放的路径
mysql> source /root/tuchuang/tuchuang/tuchuang_noindex.sql;   #导入不带索引的数据库，具体看自己存放的路径
```
带索引和不带索引的数据库，用于后续做性能测试，加深对索引的理解。

# 3 修改配置文件
修改tc_http_server.conf
## 配置是否生成短链,主要是将完整的文件下载路径转成短链
```
# 是否开启短链, 主要是图片分享地址，如果开启需要设置shorturl-server grpc服务地址
enable_shorturl=1
# 因为当前部署是同一台机器所以使用127.0.0.1，注意端口和shorturl-server保持一致
shorturl_server_address=127.0.0.1:50051
shorturl_server_access_token=e8n05nr9jey84prEhw5u43th0yi294780yjr3h7sksSdkFdDngKi
```

## 其他配置
具体见tc_http_server.conf根据自己服务器实际参数修改。