#pragma once

#include <string>
#include <cstdint>

using std::string;
int ApiRegisterUser(uint32_t conn_uuid, string url, string post_data);
