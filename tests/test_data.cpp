#include "json.hpp"
#include "repository.hpp"
#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace graph_tool;

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(stream.good(), "Fixture cannot be opened");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

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
        const auto text = read_file("examples/metadata/chalk-4.1.2.json");
        const auto package = parse_package(parse_json(text), "chalk", "4.1.2");
        std::ostringstream output;
        print_dependencies(package, output);
        check(output.str() == "package=chalk@4.1.2\ndirect_dependencies=2\n"
              "ansi-styles=^4.1.0\nsupports-color=^7.1.0\n", "Wrong direct dependencies");
    });
    suite.run("package without dependencies", [] {
        const auto text = read_file("examples/metadata/is-number-7.0.0.json");
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
            return read_file("examples/metadata/types-is-number-7.0.5.json");
        });
        check(repository.exact("@types/is-number", "7.0.5").dependencies.empty(), "Wrong metadata");
    });
    suite.run("local graph data", [] {
        TestRepository repository("examples/repository.json");
        const auto package = repository.exact("A", "1.0.0");
        check(package.dependencies.size() == 2, "Wrong test dependencies");
        expect_error([&] { repository.exact("Z", "1.0.0"); });
    });
}

int main() {
    TestSuite suite;
    json_tests(suite);
    metadata_tests(suite);
    return suite.finish();
}
