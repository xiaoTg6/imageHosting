#pragma once

#include <string>
#include <cstdint>

using std::string;

int ApiUpload(uint32_t conn_uuid, string url, string post_data);

int ApiUploadInit(char *dfs_path_client, char *web_server_ip, char *web_server_port,
                  char *storage_web_server_ip, char *storage_web_server_port,
                  char *shorturl_server_address, char *access_token);