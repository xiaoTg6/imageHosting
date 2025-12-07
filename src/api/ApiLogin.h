#pragma once

#include <string>
#include <cstdint>
#include "ApiCommon.h"

using std::string;

#if API_LOGIN_MUTI_THREAD
int ApiUserLogin(uint32_t conn_uuid, string url, string post_data);
#else

#endif