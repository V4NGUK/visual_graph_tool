#include "semver.hpp"
#include "test_support.hpp"

#include <tuple>
#include <vector>

using namespace graph_tool;
using Case = std::tuple<std::string, std::string, bool>;

void range_tests(TestSuite& suite) {
    const std::vector<Case> cases = {
        {"1.2.3", "1.2.3", true}, {"1.2.4", "1.2.3", false},
        {"1.9.9", "^1.2.3", true}, {"2.0.0", "^1.2.3", false},
        {"0.2.9", "^0.2.3", true}, {"0.3.0", "^0.2.3", false},
        {"0.0.3", "^0.0.3", true}, {"0.0.4", "^0.0.3", false},
        {"0.9.0", "^0", true}, {"1.0.0", "^0", false},
        {"0.0.9", "^0.0", true}, {"0.1.0", "^0.0", false},
        {"1.2.9", "~1.2.3", true}, {"1.3.0", "~1.2.3", false},
        {"1.9.9", "~1", true}, {"2.0.0", "~1", false},
        {"1.2.9", "1.2.x", true}, {"1.3.0", "1.2", false},
        {"1.9.0", "1", true}, {"2.0.0", "1.*", false},
        {"9.0.0", "*", true}, {"9.0.0", "", true},
        {"1.3.0", ">1.2", true}, {"1.2.9", ">1.2", false},
        {"1.2.9", "<=1.2", true}, {"1.3.0", "<=1.2", false},
        {"1.2.0", ">=1.2 <2.0.0", true}, {"2.0.0", ">=1.2 <2.0.0", false},
        {"1.2.0", "1.2 - 2.3", true}, {"2.3.9", "1.2 - 2.3", true},
        {"2.4.0", "1.2 - 2.3", false}, {"2.3.4", "1.2.3 - 2.3.4", true},
        {"2.3.5", "1.2.3 - 2.3.4", false}, {"3.0.0", "1.x || >=3", true},
        {"2.0.0", "1.x || >=3", false}, {"1.2.3", ">= 1.2.0 < 2.0.0", true},
        {"1.2.3-beta.2", "^1.2.3-beta.1", true}, {"1.2.4-beta.1", "^1.2.3-beta.1", false},
        {"1.2.3-beta.1", "*", false}, {"1.2.3+build.001", "=1.2.3", true},
        {"1.2.3", "v1.2.3", true}, {"1.0.0", ">*", false},
        {"0.0.0", "<*", false}, {"0.0.0", ">=*", true},
        {"1.2.3-beta.1", "1.2.3-beta.1", true}, {"1.2.3", "~1.2", true}
    };
    for (const auto& entry : cases) {
        suite.run(std::get<0>(entry) + " in " + std::get<1>(entry), [&] {
            const auto version = parse_version(std::get<0>(entry));
            check(version.has_value(), "Valid version rejected");
            check(VersionRange(std::get<1>(entry)).matches(*version) == std::get<2>(entry), "Wrong match");
        });
    }
}

void precedence_tests(TestSuite& suite) {
    const std::vector<std::string> ordered = {
        "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
        "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "2.0.0"
    };
    for (std::size_t index = 1; index < ordered.size(); ++index) {
        suite.run("precedence " + ordered[index], [&] {
            check(compare_versions(*parse_version(ordered[index - 1]), *parse_version(ordered[index])) < 0,
                  "Incorrect prerelease precedence");
        });
    }
    suite.run("build metadata does not change precedence", [] {
        check(compare_versions(*parse_version("1.0.0+a"), *parse_version("1.0.0+b")) == 0, "Build compared");
    });
    for (const auto value : {"01.0.0", "1.0.0-01", "1.0", "v1.0.0", "1.0.0+", "99999999999999999999999.0.0"}) {
        suite.run("invalid version " + std::string(value), [=] {
            check(!parse_version(value), "Invalid version accepted");
        });
    }
    for (const auto value : {"file:../a", "git+https://host/a", "latest", "^", "1.x.3", "1..2", "=>1.0.0"}) {
        suite.run("invalid range " + std::string(value), [=] {
            expect_error([&] { VersionRange range(value); });
        });
    }
}

int main() {
    TestSuite suite;
    range_tests(suite);
    precedence_tests(suite);
    return suite.finish();
}
