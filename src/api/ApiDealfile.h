#pragma once

#include <string>
#include <cstdint>

using std::string;

int ApiDealfile(uint32_t conn_uuid, string url, string post_data);
int ApiDealfileInit(char *dfs_path_client);