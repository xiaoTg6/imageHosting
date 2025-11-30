#pragma once

#include <string>
#include <cstdint>

using std::string;

int ApiMd5(uint32_t conn_uuid, string url, string post_data);