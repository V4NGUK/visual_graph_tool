#pragma once

#include <map>
#include <string>
#include <vector>

namespace graph_tool {

struct Json {
    enum class Kind { null_value, boolean, number, string, array, object };
    Kind kind = Kind::null_value;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> members;

    const std::string& as_string() const;
    const std::vector<Json>& as_array() const;
    const std::map<std::string, Json>& as_object() const;
    const Json* find(const std::string& key) const;
};

Json parse_json(const std::string& input);

}
