#pragma once

#include "repository.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace graph_tool {

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

}
