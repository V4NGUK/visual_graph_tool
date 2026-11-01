#pragma once

#include <string>

namespace graph_tool {

std::string http_get(const std::string& url);
std::string encode_component(const std::string& value);

}
