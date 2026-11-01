#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace graph_tool {

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

}
