cmake_minimum_required(VERSION 3.16)
project(tc_http_server LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


file(GLOB_RECURSE BASE_SRC   base/*.cc)    # 基础网络/工具模块
file(GLOB_RECURSE API_SRC    api/*.cc)     # 业务接口实现
file(GLOB_RECURSE LOG_SRC    log/*.cc log/*.cpp) # 异步日志
file(GLOB_RECURSE MYSQL_SRC  mysql/*.cc)   # MySQL 连接池
file(GLOB_RECURSE REDIS_SRC  redis/*.c redis/*.cc) # hiredis C 源码 + 封装
file(GLOB_RECURSE JSON_SRC   jsoncpp/*.cpp) # jsoncpp
file(GLOB ROOT_SRC *.cc)                   


add_executable(tc_http_server               # 定义可执行目标
  ${BASE_SRC} ${API_SRC} ${LOG_SRC}
  ${MYSQL_SRC} ${REDIS_SRC} ${JSON_SRC} ${ROOT_SRC}
)

target_include_directories(tc_http_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}             # 工程根
    ${CMAKE_CURRENT_SOURCE_DIR}/base
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/mysql
    ${CMAKE_CURRENT_SOURCE_DIR}/redis
    ${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp
    /usr/include/fastdfs                    # FastDFS 头
    /usr/include/fastcommon                 # FastDFS 依赖
    /usr/include/mysql                      # MySQL 头
    /usr/include/hiredis                    # hiredis 头（按实际环境调整）
)

# 编译选项与宏，仅作用于该目标
target_compile_options(tc_http_server PRIVATE
  -Wall -Wextra -Wpedantic                   # 常见警告
  -Wno-unused-parameter                      # 抑制未用参数（配合 UNUSED 宏）
  -D_REENTRANT                               # 线程相关
  -D_FILE_OFFSET_BITS=64                     # 大文件支持
  -DLINUX_DAEMON                             # 若代码中有守护进程分支
  -DAC_HAS_INFO -DAC_HAS_WARNING -DAC_HAS_ERROR -DAC_HAS_CRITICAL -DAC_HAS_DEBUG  # 日志级别宏
)

# 用 CMake 跨平台方式找线程库（代替手写 -lpthread）
find_package(Threads REQUIRED)

# 链接外部库
target_link_libraries(tc_http_server
  PRIVATE
    Threads::Threads                        # 线程库
    fdfsclient                              # FastDFS
    fastcommon                              # FastDFS 依赖
    mysqlclient                             # MySQL 客户端
    m                                       # 数学库（部分平台必需）
)

# 可执行文件名（可选，显式指定产物名）
set_target_properties(tc_http_server PROPERTIES
  OUTPUT_NAME tc_http_server
)