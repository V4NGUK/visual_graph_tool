#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
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
    bool direct = false;
    std::string reverse;
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

struct Version {
    std::array<std::uint64_t, 3> parts{};
    std::vector<std::string> prerelease;
};

std::optional<Version> parse_version(const std::string& text);
int compare_versions(const Version& left, const Version& right);

struct Comparator {
    std::string operation;
    Version version;
};

class VersionRange {
public:
    explicit VersionRange(const std::string& text);
    bool matches(const Version& version) const;

private:
    std::vector<std::vector<Comparator>> alternatives_;
};

using Resolve = std::function<Package(const std::string&, const std::string&)>;

struct GraphLimits {
    std::size_t max_nodes = 10000;
    std::size_t max_depth = 512;
};

struct DependencyGraph {
    std::string root;
    std::vector<std::string> order;
    std::map<std::string, std::vector<std::string>> edges;
};

std::string package_id(const Package& package);
bool excluded(const std::string& name, const std::string& filter);
DependencyGraph build_graph(const Package& root, Resolve resolve,
                            const std::string& filter = "", GraphLimits limits = {});
void print_graph(const DependencyGraph& graph, std::ostream& output);
DependencyGraph reverse_graph(const DependencyGraph& graph, const std::string& target, GraphLimits limits = {});
void print_reverse(const DependencyGraph& graph, std::ostream& output);
void print_dot(const DependencyGraph& graph, std::ostream& output);
void print_ascii(const DependencyGraph& graph, std::ostream& output);
void save_visualization(const DependencyGraph& graph, const std::string& filename);

}
