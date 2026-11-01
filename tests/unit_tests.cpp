#include "test_support.hpp"

#include <chrono>
#include <sstream>
#include <tuple>

using Arguments = std::vector<std::string>;
namespace fs = std::filesystem;

struct Fixture {
    fs::path directory;

    Fixture() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = fs::current_path() / fs::u8path("тестовые данные " + std::to_string(stamp));
        fs::create_directory(directory);
        std::ofstream(directory / "repository.json") << "{\"A\": [\"B\"], \"B\": []}";
        std::ofstream(directory / "empty.json");
        std::ofstream(directory / "existing.svg") << "original content";
        fs::create_directory(directory / "folder.svg");
    }

    ~Fixture() {
        std::error_code error;
        for (const auto& name : {"repository.json", "empty.json", "existing.svg", "folder.svg"}) {
            fs::remove(directory / name, error);
        }
        fs::remove(directory, error);
    }

    std::string path(const std::string& name) const {
        return (directory / name).u8string();
    }
};

Arguments base_arguments() {
    return {"--package", "express", "--version", "4.18.2"};
}

void expect_error(const Arguments& arguments, const std::string& option) {
    try {
        graph_tool::parse_arguments(arguments);
    } catch (const std::invalid_argument& error) {
        check(std::string(error.what()).find(option) != std::string::npos,
              "Error does not identify " + option);
        return;
    }
    throw std::runtime_error("Invalid arguments were accepted: " + option);
}

Arguments with_value(const std::string& option, const std::string& value) {
    auto arguments = base_arguments();
    if (option == "--package") {
        arguments[1] = value;
    } else if (option == "--version") {
        arguments[3] = value;
    } else {
        arguments.insert(arguments.end(), {option, value});
    }
    return arguments;
}

void test_defaults(TestSuite& suite) {
    suite.run("all seven defaults and exact output", [] {
        const auto config = graph_tool::parse_arguments(base_arguments());
        std::ostringstream output;
        graph_tool::print_config(config, output);
        check(output.str() ==
              "package=express\nrepository=https://registry.npmjs.org\n"
              "test_mode=false\nversion=4.18.2\noutput=graph.svg\nascii=false\nfilter=\n",
              "Unexpected configuration output");
    });
    suite.run("equals syntax and custom values", [] {
        const auto config = graph_tool::parse_arguments({
            "--package=@types/node", "--version=1.2.3-rc.1+build.007",
            "--repository=http://localhost:8080/npm/", "--output=custom.svg",
            "--ascii", "--filter=@types/"
        });
        check(config.package == "@types/node" && config.ascii && !config.test_mode,
              "Wrong package or flags");
        check(config.version == "1.2.3-rc.1+build.007" && config.output == "custom.svg",
              "Wrong version or output");
        check(config.repository == "http://localhost:8080/npm/" && config.filter == "@types/",
              "Wrong repository or filter");
    });
    suite.run("help without required options", [] {
        check(graph_tool::parse_arguments({"--help"}).help, "Missing help flag");
        check(graph_tool::parse_arguments({"-h"}).help, "Missing short help flag");
    });
}

void test_names_and_versions(TestSuite& suite) {
    for (const auto& name : {"express", "@types/node", "@scope/.name", "@scope/_name", "a-b.c_d"}) {
        suite.run("valid package " + std::string(name), [=] {
            graph_tool::parse_arguments(with_value("--package", name));
        });
    }
    for (const auto& name : {"", "Express", "a b", "@scope", ".name", "_name", "a/b", "пакет"}) {
        suite.run("invalid package " + std::string(name), [=] {
            expect_error(with_value("--package", name), "--package");
        });
    }
    suite.run("package length boundary", [] {
        graph_tool::parse_arguments(with_value("--package", std::string(214, 'a')));
        expect_error(with_value("--package", std::string(215, 'a')), "--package");
    });
    for (const auto& version : {"0.0.0", "1.2.3", "1.2.3-alpha.1", "1.2.3+001", "1.2.3-0.1a+abc"}) {
        suite.run("valid version " + std::string(version), [=] {
            graph_tool::parse_arguments(with_value("--version", version));
        });
    }
    for (const auto& version : {"", "latest", "1.2", "v1.2.3", "01.2.3", "1.2.3-01",
                                "1.2.3-", "1.2.3+", "1.2.3-alpha..1", "^1.2.3", "1.2.3 "}) {
        suite.run("invalid version " + std::string(version), [=] {
            expect_error(with_value("--version", version), "--version");
        });
    }
}

void test_repository(TestSuite& suite, const Fixture& fixture) {
    for (const auto& url : {"https://registry.npmjs.org", "http://localhost:65535/npm/",
                           "http://127.0.0.1:8080", "https://example.com/a%20b"}) {
        suite.run("valid repository " + std::string(url), [=] {
            graph_tool::parse_arguments(with_value("--repository", url));
        });
    }
    for (const auto& url : {"", "registry.npmjs.org", "ftp://example.com", "https://",
                           "https://a..b", "https://-bad.com", "https://host:0",
                           "https://host:65536", "https://host:abc", "https://a/b c",
                           "https://a/%", "https://a/%Q1", "https://a/%1Q"}) {
        suite.run("invalid repository " + std::string(url), [=] {
            expect_error(with_value("--repository", url), "--repository");
        });
    }
    suite.run("test mode and Unicode path", [&] {
        const auto config = graph_tool::parse_arguments({
            "--package", "A", "--version", "1.0.0", "--repository",
            fixture.path("repository.json"), "--test-mode"
        });
        check(config.test_mode && config.package == "A", "Test mode not applied");
    });
    for (const auto& file : {"missing.json", "empty.json", "folder.svg"}) {
        suite.run("invalid test repository " + std::string(file), [&fixture, file] {
            expect_error({"--package", "A", "--version", "1.0.0", "--test-mode",
                          "--repository", fixture.path(file)}, "--repository");
        });
    }
    suite.run("test mode rejects lowercase names and URL", [] {
        expect_error({"--package", "a", "--version", "1.0.0", "--test-mode"}, "--package");
        expect_error({"--package", "A", "--version", "1.0.0", "--test-mode"}, "--repository");
    });
}

void test_output_and_filter(TestSuite& suite, const Fixture& fixture) {
    for (const auto& file : {"", "graph.png", ".svg", "CON.svg", "graph?.svg"}) {
        suite.run("invalid output " + std::string(file), [=] {
            expect_error(with_value("--output", file), "--output");
        });
    }
    for (const auto& file : {"folder.svg", "missing/graph.svg"}) {
        suite.run("invalid output path " + std::string(file), [&fixture, file] {
            expect_error(with_value("--output", fixture.path(file)), "--output");
        });
    }
    suite.run("output validation does not create or overwrite files", [&] {
        graph_tool::parse_arguments(with_value("--output", fixture.path("existing.svg")));
        graph_tool::parse_arguments(with_value("--output", fixture.path("граф.svg")));
        std::ifstream input(fs::u8path(fixture.path("existing.svg")));
        std::string contents;
        std::getline(input, contents);
        check(contents == "original content", "Existing file was overwritten");
        check(!fs::exists(fs::u8path(fixture.path("граф.svg"))), "Unexpected output file");
    });
    for (const auto& filter : {"with space", "\t", "\n"}) {
        suite.run("invalid filter whitespace", [=] {
            expect_error(with_value("--filter", filter), "--filter");
        });
    }
    suite.run("filter boundaries and empty filter", [] {
        graph_tool::parse_arguments(with_value("--filter", ""));
        graph_tool::parse_arguments(with_value("--filter", std::string(214, 'a')));
        expect_error(with_value("--filter", std::string(215, 'a')), "--filter");
    });
}

void test_parser_errors(TestSuite& suite) {
    for (const auto& option : {"--package", "--repository", "--version", "--output", "--filter"}) {
        suite.run("missing value " + std::string(option), [=] {
            expect_error({option}, option);
            expect_error({option, "--ascii"}, option);
        });
    }
    for (const auto& option : {"--package", "--repository", "--version", "--output", "--filter",
                              "--test-mode", "--ascii"}) {
        suite.run("duplicate option " + std::string(option), [=] {
            const bool flag = std::string(option) == "--ascii" || std::string(option) == "--test-mode";
            expect_error(flag ? Arguments{option, option} : Arguments{option, "x", option, "y"}, option);
        });
    }
    for (const auto& flag : {"--test-mode", "--ascii"}) {
        suite.run("flags reject values " + std::string(flag), [=] {
            expect_error({std::string(flag) + "=false"}, flag);
            expect_error({flag, "true"}, "true");
        });
    }
    suite.run("required options and unknown arguments", [] {
        expect_error({}, "--package");
        expect_error({"--version", "1.2.3"}, "--package");
        expect_error({"--package", "express"}, "--version");
        expect_error({"--unknown"}, "--unknown");
        expect_error({"express"}, "express");
    });
}

using namespace graph_tool;

void json_tests(TestSuite& suite) {
    suite.run("JSON values and Unicode escapes", [] {
        const auto json = parse_json(R"({"name":"a\u002fb", "items":[true,false,null,-1.2e3]})");
        check(json.find("name")->as_string() == "a/b", "String escape lost");
        check(json.find("items")->as_array().size() == 4, "Array lost");
        check(parse_json(R"("\uD83D\uDE00")").as_string() == "\xf0\x9f\x98\x80", "Surrogate pair lost");
        check(parse_json("\xef\xbb\xbf{}").as_object().empty(), "UTF-8 BOM rejected");
    });
    for (const auto input : {"", "{", "[1,]", "{\"a\":1,}", "{}{}", "01", "1.", "--1", "NaN",
                             "{\"a\":1,\"a\":2}", "\"\\q\"", "\"\\uD800\"", "\"\\uDC00\""}) {
        suite.run("reject invalid JSON: " + std::string(input), [=] {
            expect_error([&] { parse_json(input); });
        });
    }
    suite.run("JSON types and depth limits", [] {
        expect_error([] { parse_json("[]").as_object(); });
        expect_error([] { parse_json("null").as_string(); });
        expect_error([] { parse_json(std::string(130, '[') + "0" + std::string(130, ']')); });
    });
}

void metadata_tests(TestSuite& suite) {
    suite.run("real chalk metadata and exact output", [] {
        const auto text = metadata_text("chalk");
        const auto package = parse_package(parse_json(text), "chalk", "4.1.2");
        std::ostringstream output;
        print_dependencies(package, output);
        check(output.str() == "package=chalk@4.1.2\ndirect_dependencies=2\n"
              "ansi-styles=^4.1.0\nsupports-color=^7.1.0\n", "Wrong direct dependencies");
    });
    suite.run("package without dependencies", [] {
        const auto text = metadata_text("is-number");
        check(parse_package(parse_json(text), "is-number", "7.0.0").dependencies.empty(), "Unexpected edges");
    });
    for (const auto text : {R"({})", R"({"name":"other","version":"1.0.0"})",
                            R"({"name":"x","version":"2.0.0"})",
                            R"({"name":"x","version":"1.0.0","dependencies":[]})",
                            R"({"name":"x","version":"1.0.0","dependencies":{"y":12}})"}) {
        suite.run("reject malformed metadata", [=] {
            expect_error([&] { parse_package(parse_json(text), "x", "1.0.0"); });
        });
    }
    suite.run("scoped names and custom registry path", [] {
        NpmRepository repository("http://localhost:8080/npm/", [](const std::string& url) {
            check(url == "http://localhost:8080/npm/%40types%2Fis-number/7.0.5", "Wrong request URL");
            return metadata_text("@types/is-number");
        });
        check(repository.exact("@types/is-number", "7.0.5").dependencies.empty(), "Wrong metadata");
    });
    suite.run("local graph data", [] {
        TestRepository repository("examples/repository.json");
        const auto package = repository.exact("A", "1.0.0");
        check(package.dependencies.size() == 2, "Wrong test dependencies");
        expect_error([&] { repository.exact("MISSING", "1.0.0"); });
    });
}

using namespace graph_tool;

DependencyGraph test_graph(const std::string& package, const std::string& filter = "", GraphLimits limits = {}) {
    TestRepository repository("examples/repository.json");
    return build_graph(repository.exact(package, "1.0.0"), [&](const auto& name, const auto& version) {
        return repository.exact(name, version);
    }, filter, limits);
}

void traversal_tests(TestSuite& suite) {
    suite.run("recursive BFS visits shared dependencies once", [] {
        const auto graph = test_graph("A");
        const std::vector<std::string> expected = {"A@1.0.0", "B@1.0.0", "C@1.0.0", "D@1.0.0"};
        check(graph.order == expected, "Incorrect BFS order");
        check(graph.edges.at("B@1.0.0") == graph.edges.at("C@1.0.0"), "Shared edge lost");
    });
    suite.run("cycles terminate and preserve back edges", [] {
        const auto graph = test_graph("X");
        check(graph.order.size() == 3, "Cycle was revisited");
        check(graph.edges.at("Z@1.0.0") == std::vector<std::string>{"X@1.0.0"}, "Back edge lost");
    });
    suite.run("self loops and duplicate edges", [] {
        const auto graph = test_graph("S");
        check(graph.order.size() == 1 && graph.edges.at("S@1.0.0").size() == 1, "Loop duplicated");
    });
    suite.run("unreachable packages are not included", [] {
        check(test_graph("I").order.size() == 1, "Unreachable vertex included");
    });
    suite.run("filter removes matching packages and their unique descendants", [] {
        const auto graph = test_graph("X", "Y");
        check(graph.order.size() == 1 && graph.edges.at("X@1.0.0").empty(), "Filtered node included");
        check(test_graph("A", "B").order.size() == 3, "Reachable node removed");
        check(test_graph("A", "A").order.empty(), "Filtered root included");
    });
    suite.run("depth and node limits fail instead of truncating", [] {
        expect_error([] { test_graph("X", "", {2, 10}); });
        expect_error([] { test_graph("X", "", {10, 1}); });
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
            const auto name = url.substr(url.rfind('/') + 1);
            return metadata_versions(name);
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
    Fixture fixture;
    test_defaults(suite);
    test_names_and_versions(suite);
    test_repository(suite, fixture);
    test_output_and_filter(suite, fixture);
    test_parser_errors(suite);
    json_tests(suite);
    metadata_tests(suite);
    traversal_tests(suite);
    identity_tests(suite);
    resolution_tests(suite);
    npm_graph_test(suite);
    range_tests(suite);
    precedence_tests(suite);
    return suite.finish();
}
