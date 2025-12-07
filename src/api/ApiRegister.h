#pragma once

#include <string>
#include <cstdint>
#include "ApiCommon.h"

using std::string;
#if API_REGISTER_MUTI_THREAD
int ApiRegisterUser(uint32_t conn_uuid, string url, string post_data);
#else
int ApiRegisterUser(string &url, string &post_data, string &str_json);

#endif
