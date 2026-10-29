#include "config.hpp"
#include "test_support.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

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

}

int main() {
    TestSuite suite;
    Fixture fixture;
    test_defaults(suite);
    test_names_and_versions(suite);
    test_repository(suite, fixture);
    test_output_and_filter(suite, fixture);
    test_parser_errors(suite);
    return suite.finish();
}
