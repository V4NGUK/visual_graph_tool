#include "app.hpp"

#include <algorithm>
#include <limits>
#include <ostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace graph_tool {

namespace {

std::vector<std::string> split(const std::string& text, const std::string& separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    auto end = text.find(separator);
    while (end != std::string::npos) {
        result.push_back(text.substr(start, end - start));
        start = end + separator.size();
        end = text.find(separator, start);
    }
    result.push_back(text.substr(start));
    return result;
}

bool numeric(const std::string& value) {
    return !value.empty() && value.find_first_not_of("0123456789") == std::string::npos;
}

int compare_identifier(const std::string& left, const std::string& right) {
    const bool left_numeric = numeric(left);
    const bool right_numeric = numeric(right);
    if (left_numeric != right_numeric) {
        return left_numeric ? -1 : 1;
    }
    if (left_numeric && left.size() != right.size()) {
        return left.size() < right.size() ? -1 : 1;
    }
    if (left == right) {
        return 0;
    }
    return left < right ? -1 : 1;
}

struct Partial {
    Version version;
    std::size_t count = 0;
};

bool wildcard_component(const std::string& value) {
    return value == "*" || value == "x" || value == "X";
}

std::uint64_t numeric_component(const std::string& value) {
    if (!numeric(value) || (value.size() > 1 && value.front() == '0')) {
        throw std::runtime_error("Неподдерживаемая или неверная часть версии: " + value);
    }
    return std::stoull(value);
}

Partial parse_partial_version(std::string text) {
    if (!text.empty() && text.front() == 'v') {
        text.erase(0, 1);
    }
    if (const auto full = parse_version(text)) {
        return {*full, 3};
    }
    Partial result;
    const auto pieces = split(text, ".");
    if (pieces.size() > result.version.parts.size()) {
        throw std::runtime_error("Некорректный диапазон версий: " + text);
    }
    bool wildcard = false;
    for (const auto& piece : pieces) {
        if (wildcard_component(piece)) {
            wildcard = true;
        } else {
            if (wildcard) {
                throw std::runtime_error("Неподдерживаемая или неверная версия: " + text);
            }
            result.version.parts[result.count++] = numeric_component(piece);
        }
    }
    return result;
}

Version next_version(Version value, std::size_t index) {
    if (value.parts[index] == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("Слишком большой номер версии");
    }
    ++value.parts[index];
    std::fill(value.parts.begin() + index + 1, value.parts.end(), 0);
    value.prerelease.clear();
    return value;
}

void add_bounded_range(std::vector<Comparator>& result, const Partial& value, char kind) {
    if (value.count == 0) {
        return;
    }
    std::size_t upper_index = value.count == 1 ? 0 : 1;
    if (kind == '^') {
        upper_index = 0;
        while (upper_index + 1 < value.count && value.version.parts[upper_index] == 0) {
            ++upper_index;
        }
    }
    result.push_back({">=", value.version});
    result.push_back({"<", next_version(value.version, upper_index)});
}

void add_comparison(std::vector<Comparator>& result, const Partial& value, const std::string& operation) {
    if (value.count == 0) {
        if (operation == "<" || operation == ">") {
            result.push_back({"<", Version{}});
        }
    } else if (value.count == 3) {
        result.push_back({operation, value.version});
    } else if (operation == "=") {
        result.push_back({">=", value.version});
        result.push_back({"<", next_version(value.version, value.count - 1)});
    } else if (operation == ">" || operation == "<=") {
        result.push_back({operation == ">" ? ">=" : "<", next_version(value.version, value.count - 1)});
    } else {
        result.push_back({operation, value.version});
    }
}

void parse_comparator(std::vector<Comparator>& result, const std::string& token) {
    const auto start = token.find_first_not_of("<>=~^");
    if (start == std::string::npos) {
        throw std::runtime_error("Диапазон без версии: " + token);
    }
    auto operation = token.substr(0, start);
    const auto value = parse_partial_version(token.substr(start));
    if (operation == "~" || operation == "^") {
        add_bounded_range(result, value, operation.front());
        return;
    }
    if (operation.empty()) {
        operation = "=";
    }
    const std::vector<std::string> supported = {"=", "<", ">", "<=", ">="};
    if (std::find(supported.begin(), supported.end(), operation) == supported.end()) {
        throw std::runtime_error("Неподдерживаемый оператор версии: " + operation);
    }
    add_comparison(result, value, operation);
}

std::vector<Comparator> parse_group(const std::string& text) {
    std::vector<Comparator> result;
    static const std::regex hyphen(R"(^\s*(\S+)\s+-\s+(\S+)\s*$)");
    std::smatch match;
    if (std::regex_match(text, match, hyphen)) {
        const auto low = parse_partial_version(match[1]);
        const auto high = parse_partial_version(match[2]);
        if (low.count != 0) {
            result.push_back({">=", low.version});
        }
        if (high.count == 3) {
            result.push_back({"<=", high.version});
        } else if (high.count != 0) {
            result.push_back({"<", next_version(high.version, high.count - 1)});
        }
        return result;
    }
    static const std::regex operator_spaces(R"((<=|>=|<|>|=|~|\^)\s+)");
    std::istringstream input(std::regex_replace(text, operator_spaces, "$1"));
    std::string token;
    while (input >> token) {
        parse_comparator(result, token);
    }
    return result;
}

bool accepts(const Comparator& comparator, const Version& candidate) {
    const auto order = compare_versions(candidate, comparator.version);
    if (comparator.operation == "<") return order < 0;
    if (comparator.operation == "<=") return order <= 0;
    if (comparator.operation == ">") return order > 0;
    if (comparator.operation == ">=") return order >= 0;
    return order == 0;
}

bool accepts_group(const std::vector<Comparator>& group, const Version& version) {
    bool prerelease_allowed = version.prerelease.empty();
    for (const auto& comparator : group) {
        if (!accepts(comparator, version)) {
            return false;
        }
        if (!comparator.version.prerelease.empty() && comparator.version.parts == version.parts) {
            prerelease_allowed = true;
        }
    }
    return prerelease_allowed;
}

}

std::optional<Version> parse_version(const std::string& text) {
    static const std::regex pattern(
        R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))"
        R"((?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?)"
        R"((?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?)");
    std::smatch match;
    if (!std::regex_match(text, match, pattern)) {
        return std::nullopt;
    }
    Version result;
    try {
        for (std::size_t index = 0; index < result.parts.size(); ++index) {
            result.parts[index] = std::stoull(match[index + 1]);
        }
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
    if (match[4].matched) {
        result.prerelease = split(match[4], ".");
        for (const auto& identifier : result.prerelease) {
            if (numeric(identifier) && identifier.size() > 1 && identifier.front() == '0') {
                return std::nullopt;
            }
        }
    }
    return result;
}

int compare_versions(const Version& left, const Version& right) {
    if (left.parts != right.parts) {
        return left.parts < right.parts ? -1 : 1;
    }
    if (left.prerelease.empty() != right.prerelease.empty()) {
        return left.prerelease.empty() ? 1 : -1;
    }
    const auto common = std::min(left.prerelease.size(), right.prerelease.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto order = compare_identifier(left.prerelease[index], right.prerelease[index]);
        if (order != 0) {
            return order;
        }
    }
    if (left.prerelease.size() == right.prerelease.size()) {
        return 0;
    }
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

VersionRange::VersionRange(const std::string& text) {
    for (const auto& group : split(text, "||")) {
        alternatives_.push_back(parse_group(group));
    }
}

bool VersionRange::matches(const Version& version) const {
    for (const auto& group : alternatives_) {
        if (accepts_group(group, version)) {
            return true;
        }
    }
    return false;
}

namespace {

std::pair<std::string, std::string> alias(const std::string& name, const std::string& requirement) {
    if (requirement.rfind("npm:", 0) != 0) {
        return {name, requirement};
    }
    const auto target = requirement.substr(4);
    const auto separator = target.rfind('@');
    if (separator == std::string::npos || separator == 0) {
        return {target, "*"};
    }
    return {target.substr(0, separator), target.substr(separator + 1)};
}

std::string resolve_tag(const Json& document, const std::string& requirement) {
    if (const auto tags = document.find("dist-tags")) {
        if (const auto tag = tags->find(requirement)) {
            return tag->as_string();
        }
    }
    return requirement;
}

Package select_version(const Json& document, const std::string& name, const std::string& requirement) {
    const auto versions = document.find("versions");
    if (!versions) {
        throw std::runtime_error("В метаданных нет списка versions для " + name);
    }
    const VersionRange range(resolve_tag(document, requirement));
    std::optional<Version> best;
    std::string selected;
    for (const auto& [number, metadata] : versions->as_object()) {
        const auto version = parse_version(number);
        if (!version || !range.matches(*version)) {
            continue;
        }
        if (best && compare_versions(*best, *version) >= 0) {
            continue;
        }
        best = version;
        selected = number;
    }
    if (selected.empty()) {
        throw std::runtime_error("Нет подходящей версии для " + name + '@' + requirement);
    }
    return parse_package(versions->as_object().at(selected), name, selected);
}

}

Package NpmRepository::resolve(const std::string& name, const std::string& requirement) {
    const auto [package, range] = alias(name, requirement);
    try {
        if (cache_.count(package) == 0) {
            const auto endpoint = url_ + '/' + encode_component(package);
            cache_[package] = parse_json(fetch_(endpoint));
        }
        return select_version(cache_.at(package), package, range);
    } catch (const std::exception& error) {
        throw std::runtime_error("Зависимость " + name + '@' + requirement + ": " + error.what());
    }
}

namespace {

struct Search {
    Resolve resolve;
    std::string filter;
    GraphLimits limits;
    DependencyGraph graph;
    std::set<std::string> seen;
    std::map<std::pair<std::string, std::string>, Package> cache;
};

Package load_package(Search& search, const std::string& name, const std::string& range) {
    const auto key = std::make_pair(name, range);
    if (search.cache.count(key) == 0) {
        search.cache[key] = search.resolve(name, range);
    }
    return search.cache.at(key);
}

void add_to_level(Search& search, const Package& package, std::vector<Package>& level) {
    const auto id = package_id(package);
    if (search.seen.count(id) != 0) return;
    if (search.seen.size() >= search.limits.max_nodes) {
        throw std::runtime_error("Превышен предел числа вершин графа");
    }
    search.seen.insert(id);
    level.push_back(package);
}

void expand(Search& search, const Package& package, std::vector<Package>& next) {
    auto& edges = search.graph.edges[package_id(package)];
    for (const auto& [name, range] : package.dependencies) {
        if (excluded(name, search.filter)) continue;
        const auto child = load_package(search, name, range);
        if (excluded(child.name, search.filter)) continue;
        const auto id = package_id(child);
        if (std::find(edges.begin(), edges.end(), id) == edges.end()) {
            edges.push_back(id);
        }
        add_to_level(search, child, next);
    }
}

void visit_level(Search& search, const std::vector<Package>& level, std::size_t depth) {
    if (level.empty()) return;
    if (depth > search.limits.max_depth) {
        throw std::runtime_error("Превышен предел глубины рекурсивного BFS");
    }
    std::vector<Package> next;
    for (const auto& package : level) {
        search.graph.order.push_back(package_id(package));
        expand(search, package, next);
    }
    visit_level(search, next, depth + 1);
}

void print_list(const std::vector<std::string>& names, std::ostream& output) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) output << ',';
        output << names[index];
    }
    output << '\n';
}

}

std::string package_id(const Package& package) {
    return package.name + '@' + package.version;
}

bool excluded(const std::string& name, const std::string& filter) {
    return !filter.empty() && name.find(filter) != std::string::npos;
}

DependencyGraph build_graph(const Package& root, Resolve resolve, const std::string& filter, GraphLimits limits) {
    Search search;
    search.resolve = std::move(resolve);
    search.filter = filter;
    search.limits = limits;
    search.graph.root = package_id(root);
    if (excluded(root.name, filter)) return search.graph;
    search.cache[{root.name, root.version}] = root;
    std::vector<Package> first_level;
    add_to_level(search, root, first_level);
    visit_level(search, first_level, 0);
    return search.graph;
}

void print_graph(const DependencyGraph& graph, std::ostream& output) {
    std::size_t edges = 0;
    for (const auto& entry : graph.edges) edges += entry.second.size();
    output << "root=" << graph.root << "\nnodes=" << graph.order.size() << "\nedges=" << edges << "\nbfs=";
    print_list(graph.order, output);
    for (const auto& id : graph.order) {
        output << id << " -> ";
        const auto& children = graph.edges.at(id);
        if (children.empty()) output << '-';
        print_list(children, output);
    }
}

}
