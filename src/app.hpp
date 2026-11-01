#pragma once

#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace graph_tool {

struct Config {
    std::string package;
    std::string repository = "https://registry.npmjs.org";
    bool test_mode = false;
    std::string version;
    std::string output = "graph.svg";
    bool ascii = false;
    std::string filter;
    bool help = false;
};

Config parse_arguments(const std::vector<std::string>& arguments);
void validate_config(const Config& config);
void print_config(const Config& config, std::ostream& output);
void print_help(std::ostream& output);

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

std::string http_get(const std::string& url);
std::string encode_component(const std::string& value);

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

private:
    std::string url_;
    Fetch fetch_;
};

class TestRepository {
public:
    explicit TestRepository(const std::string& filename);
    Package exact(const std::string& name, const std::string& version) const;

private:
    std::map<std::string, std::vector<std::string>> packages_;
};

}
