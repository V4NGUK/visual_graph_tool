#include "repository.hpp"
#include "semver.hpp"

#include <stdexcept>

namespace graph_tool {
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

}
