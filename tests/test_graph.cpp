#include "graph.hpp"
#include "test_support.hpp"

#include <fstream>
#include <iterator>
#include <sstream>

using namespace graph_tool;

DependencyGraph test_graph(const std::string& file, const std::string& filter = "", GraphLimits limits = {}) {
    TestRepository repository(file);
    return build_graph(repository.exact("A", "1.0.0"), [&](const auto& name, const auto& version) {
        return repository.exact(name, version);
    }, filter, limits);
}

void traversal_tests(TestSuite& suite) {
    suite.run("recursive BFS visits shared dependencies once", [] {
        const auto graph = test_graph("examples/branching.json");
        const std::vector<std::string> expected = {"A@1.0.0", "B@1.0.0", "C@1.0.0", "D@1.0.0"};
        check(graph.order == expected, "Incorrect BFS order");
        check(graph.edges.at("B@1.0.0") == graph.edges.at("C@1.0.0"), "Shared edge lost");
    });
    suite.run("cycles terminate and preserve back edges", [] {
        const auto graph = test_graph("examples/cycle.json");
        check(graph.order.size() == 3, "Cycle was revisited");
        check(graph.edges.at("C@1.0.0") == std::vector<std::string>{"A@1.0.0"}, "Back edge lost");
    });
    suite.run("self loops and duplicate edges", [] {
        const auto graph = test_graph("examples/self-loop.json");
        check(graph.order.size() == 1 && graph.edges.at("A@1.0.0").size() == 1, "Loop duplicated");
    });
    suite.run("unreachable packages are not included", [] {
        check(test_graph("examples/isolated.json").order.size() == 1, "Unreachable vertex included");
    });
    suite.run("filter removes matching packages and their unique descendants", [] {
        const auto graph = test_graph("examples/cycle.json", "B");
        check(graph.order.size() == 1 && graph.edges.at("A@1.0.0").empty(), "Filtered node included");
        check(test_graph("examples/branching.json", "B").order.size() == 3, "Reachable node removed");
        check(test_graph("examples/branching.json", "A").order.empty(), "Filtered root included");
    });
    suite.run("depth and node limits fail instead of truncating", [] {
        expect_error([] { test_graph("examples/cycle.json", "", {2, 10}); });
        expect_error([] { test_graph("examples/cycle.json", "", {10, 1}); });
    });
}

void identity_tests(TestSuite& suite) {
    suite.run("multiple versions have distinct vertices", [] {
        const Package root{"A", "1.0.0", {{"B", "1.0.0"}, {"C", "1.0.0"}}};
        const auto graph = build_graph(root, [](const auto& name, const auto& version) {
            if (name == "B") return Package{name, version, {{"D", "1.0.0"}}};
            if (name == "C") return Package{name, version, {{"D", "2.0.0"}}};
            return Package{name, version, {}};
        });
        check(graph.order.size() == 5, "Different versions were merged");
        check(graph.edges.count("D@1.0.0") && graph.edges.count("D@2.0.0"), "Missing version");
    });
    suite.run("filtered dependency is never requested", [] {
        const auto graph = build_graph({"A", "1.0.0", {{"BLOCK", "1.0.0"}}},
            [](const auto&, const auto&) -> Package {
                throw std::runtime_error("Filtered dependency requested");
            }, "LOC");
        check(graph.order.size() == 1, "Filter does not match substrings");
    });
    suite.run("shared dependency is resolved once for an identical request", [] {
        int calls = 0;
        const auto graph = build_graph({"A", "1.0.0", {{"B", "1.0.0"}, {"C", "1.0.0"}}},
            [&](const auto& name, const auto& version) {
                if (name == "D") {
                    ++calls;
                    return Package{name, version, {}};
                }
                return Package{name, version, {{"D", "1.0.0"}}};
            });
        check(graph.order.size() == 4 && calls == 1, "Shared package was fetched twice");
    });
}

void resolution_tests(TestSuite& suite) {
    suite.run("highest matching version, tags, aliases and metadata cache", [] {
        int requests = 0;
        NpmRepository repository("https://example.test", [&](const std::string& url) {
            ++requests;
            check(url == "https://example.test/x", "Alias package not resolved");
            return R"({"name":"x","dist-tags":{"next":"2.0.0-beta.1"},"versions":{
                "1.2.0":{"name":"x","version":"1.2.0"},
                "1.10.0":{"name":"x","version":"1.10.0"},
                "2.0.0-beta.1":{"name":"x","version":"2.0.0-beta.1"},
                "2.0.0":{"name":"x","version":"2.0.0"}}})";
        });
        check(repository.resolve("x", "^1").version == "1.10.0", "Wrong semantic sort");
        check(repository.resolve("x", "next").version == "2.0.0-beta.1", "Tag resolution failed");
        check(repository.resolve("alias", "npm:x@~1.2").version == "1.2.0", "Alias resolution failed");
        check(requests == 1, "Packument cache missed");
        expect_error([&] { repository.resolve("x", "^9"); });
        expect_error([&] { repository.resolve("x", "file:../x"); });
    });
}

void npm_graph_test(TestSuite& suite) {
    suite.run("actual chalk metadata traversed without network", [] {
        NpmRepository repository("https://example.test", [](const std::string& url) {
            const std::map<std::string, std::string> versions = {
                {"chalk", "4.1.2"}, {"ansi-styles", "4.3.0"}, {"supports-color", "7.2.0"},
                {"color-convert", "2.0.1"}, {"color-name", "1.1.4"}, {"has-flag", "4.0.0"}
            };
            const auto name = url.substr(url.rfind('/') + 1);
            const auto version = versions.at(name);
            std::ifstream input("examples/metadata/" + name + '-' + version + ".json");
            check(input.good(), "Missing saved metadata");
            const std::string body{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            return "{\"versions\":{\"" + version + "\":" + body + "}}";
        });
        const auto root = repository.resolve("chalk", "4.1.2");
        const auto graph = build_graph(root, [&](const auto& name, const auto& range) {
            return repository.resolve(name, range);
        });
        std::ostringstream output;
        print_graph(graph, output);
        check(output.str().find("nodes=6\nedges=5") != std::string::npos, "Wrong npm graph");
        check(graph.order[4] == "has-flag@4.0.0", "Traversal is not breadth first");
    });
}

int main() {
    TestSuite suite;
    traversal_tests(suite);
    identity_tests(suite);
    resolution_tests(suite);
    npm_graph_test(suite);
    return suite.finish();
}
