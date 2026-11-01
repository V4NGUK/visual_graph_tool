#pragma once

#include "http.hpp"
#include "json.hpp"

#include <functional>
#include <iosfwd>
#include <map>
#include <string>

namespace graph_tool {

struct Package {
    std::string name;
    std::string version;
    std::map<std::string, std::string> dependencies;
};

using Fetch = std::function<std::string(const std::string&)>;

Package parse_package(const Json& document, const std::string& name, const std::string& version);
void print_dependencies(const Package& package, std::ostream& output);

class NpmRepository {
public:
    explicit NpmRepository(std::string url, Fetch fetch = http_get);
    Package exact(const std::string& name, const std::string& version);
    Package resolve(const std::string& name, const std::string& requirement);

private:
    std::string url_;
    Fetch fetch_;
    std::map<std::string, Json> cache_;
};

class TestRepository {
public:
    explicit TestRepository(const std::string& filename);
    Package exact(const std::string& name, const std::string& version) const;

private:
    std::map<std::string, std::vector<std::string>> packages_;
};

}
