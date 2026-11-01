#include "graph.hpp"

#include <algorithm>
#include <ostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace graph_tool {
namespace {

class BreadthFirst {
public:
    BreadthFirst(Resolve resolve, std::string filter, GraphLimits limits)
        : resolve_(std::move(resolve)), filter_(std::move(filter)), limits_(limits) {}

    DependencyGraph build(const Package& root) {
        graph_.root = package_id(root);
        if (excluded(root.name, filter_)) {
            return graph_;
        }
        std::vector<Package> first_level;
        enqueue(root, first_level);
        resolved_[{root.name, root.version}] = root;
        visit_level(first_level, 0);
        return graph_;
    }

private:
    Resolve resolve_;
    std::string filter_;
    GraphLimits limits_;
    DependencyGraph graph_;
    std::set<std::string> seen_;
    std::map<std::pair<std::string, std::string>, Package> resolved_;

    Package resolve(const std::string& name, const std::string& version) {
        const auto key = std::make_pair(name, version);
        if (resolved_.count(key) == 0) {
            resolved_[key] = resolve_(name, version);
        }
        return resolved_.at(key);
    }

    void enqueue(const Package& package, std::vector<Package>& next) {
        const auto id = package_id(package);
        if (seen_.count(id) != 0) {
            return;
        }
        if (seen_.size() >= limits_.max_nodes) {
            throw std::runtime_error("Превышен предел числа вершин графа");
        }
        seen_.insert(id);
        next.push_back(package);
    }

    void expand(const Package& package, std::vector<Package>& next) {
        auto& edges = graph_.edges[package_id(package)];
        for (const auto& [name, range] : package.dependencies) {
            if (excluded(name, filter_)) {
                continue;
            }
            const auto child = resolve(name, range);
            if (excluded(child.name, filter_)) {
                continue;
            }
            const auto id = package_id(child);
            if (std::find(edges.begin(), edges.end(), id) == edges.end()) {
                edges.push_back(id);
            }
            enqueue(child, next);
        }
    }

    void visit_level(const std::vector<Package>& frontier, std::size_t depth) {
        if (frontier.empty()) {
            return;
        }
        if (depth > limits_.max_depth) {
            throw std::runtime_error("Превышен предел глубины рекурсивного BFS");
        }
        std::vector<Package> next;
        for (const auto& package : frontier) {
            graph_.order.push_back(package_id(package));
            expand(package, next);
        }
        visit_level(next, depth + 1);
    }
};

}

std::string package_id(const Package& package) {
    return package.name + '@' + package.version;
}

bool excluded(const std::string& name, const std::string& filter) {
    return !filter.empty() && name.find(filter) != std::string::npos;
}

DependencyGraph build_graph(const Package& root, Resolve resolve, const std::string& filter, GraphLimits limits) {
    return BreadthFirst(std::move(resolve), filter, limits).build(root);
}

void print_graph(const DependencyGraph& graph, std::ostream& output) {
    std::size_t edge_count = 0;
    for (const auto& entry : graph.edges) {
        edge_count += entry.second.size();
    }
    output << "root=" << graph.root << "\nnodes=" << graph.order.size() << "\nedges=" << edge_count << "\nbfs=";
    for (std::size_t index = 0; index < graph.order.size(); ++index) {
        output << (index == 0 ? "" : ",") << graph.order[index];
    }
    output << '\n';
    for (const auto& id : graph.order) {
        output << id << " -> ";
        const auto& children = graph.edges.at(id);
        if (children.empty()) {
            output << '-';
        }
        for (std::size_t index = 0; index < children.size(); ++index) {
            output << (index == 0 ? "" : ",") << children[index];
        }
        output << '\n';
    }
}

}
