#pragma once

#include "app.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

inline void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void expect_error(const std::function<void()>& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Expected an exception");
}

struct TestSuite {
    int passed = 0;
    int failed = 0;

    void run(const std::string& name, const std::function<void()>& action) {
        try {
            action();
            ++passed;
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << "Passed: " << passed << ", failed: " << failed << '\n';
        return failed == 0 ? 0 : 1;
    }
};

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(stream.good(), "Cannot open test file: " + path.u8string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

inline std::string json_string(const std::string& text) {
    const std::string hex = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char character : text) {
        if (character == '"' || character == '\\') {
            result += '\\';
            result += character;
        } else if (character < 32) {
            result += "\\u00";
            result += hex[character / 16];
            result += hex[character % 16];
        } else {
            result += character;
        }
    }
    return result + '"';
}

inline std::string json_text(const graph_tool::Json& value) {
    using Kind = graph_tool::Json::Kind;
    if (value.kind == Kind::string) return json_string(value.text);
    if (value.kind == Kind::null_value) return "null";
    if (value.kind != Kind::array && value.kind != Kind::object) return value.text;
    std::string result;
    for (const auto& [name, item] : value.members) {
        if (!result.empty()) result += ',';
        result += json_string(name) + ':' + json_text(item);
    }
    for (const auto& item : value.items) {
        if (!result.empty()) result += ',';
        result += json_text(item);
    }
    return value.kind == Kind::array ? '[' + result + ']' : '{' + result + '}';
}

inline const graph_tool::Json& saved_packages() {
    static const auto data = graph_tool::parse_json(read_file("examples/metadata.json"));
    return data.as_object().at("packages");
}

inline std::string metadata_text(const std::string& name) {
    return json_text(saved_packages().as_object().at(name));
}

inline std::string metadata_versions(const std::string& name) {
    const auto& data = saved_packages().as_object().at(name);
    const auto version = data.as_object().at("version").as_string();
    return "{\"versions\":{" + json_string(version) + ':' + json_text(data) + "}}";
}
