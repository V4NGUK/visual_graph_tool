#include "repository.hpp"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <utility>

namespace graph_tool {
namespace {

std::string required_string(const Json& document, const std::string& key) {
    const auto value = document.find(key);
    if (!value) {
        throw std::runtime_error("В метаданных отсутствует поле " + key);
    }
    return value->as_string();
}

void validate_test_name(const std::string& name) {
    static const std::regex pattern("[A-Z]+");
    if (!std::regex_match(name, pattern)) {
        throw std::runtime_error("Тестовый пакет должен называться заглавными латинскими буквами: " + name);
    }
}

std::string read_test_file(const std::string& filename) {
    std::ifstream input(std::filesystem::u8path(filename), std::ios::binary);
    if (!input) {
        throw std::runtime_error("Не удалось открыть тестовый репозиторий: " + filename);
    }
    constexpr std::size_t max_file_bytes = 4 * 1024 * 1024;
    std::string contents;
    char character = 0;
    while (input.get(character)) {
        if (contents.size() == max_file_bytes) {
            throw std::runtime_error("Тестовый репозиторий превышает 4 МБ");
        }
        contents += character;
    }
    if (!input.eof()) {
        throw std::runtime_error("Не удалось прочитать тестовый репозиторий: " + filename);
    }
    return contents;
}

std::map<std::string, std::string> read_dependencies(const Json& document) {
    std::map<std::string, std::string> result;
    const auto dependencies = document.find("dependencies");
    if (!dependencies) {
        return result;
    }
    for (const auto& [name, value] : dependencies->as_object()) {
        const auto& range = value.as_string();
        if (name.empty() || range.find_first_of("\r\n\t") != std::string::npos) {
            throw std::runtime_error("Некорректное описание зависимости");
        }
        result[name] = range;
    }
    return result;
}

}

Package parse_package(const Json& document, const std::string& name, const std::string& version) {
    Package result{required_string(document, "name"), required_string(document, "version"), {}};
    if (result.name != name || result.version != version) {
        throw std::runtime_error("Реестр вернул другой пакет или версию вместо " + name + "@" + version);
    }
    result.dependencies = read_dependencies(document);
    return result;
}

NpmRepository::NpmRepository(std::string url, Fetch fetch)
    : url_(std::move(url)), fetch_(std::move(fetch)) {
    while (!url_.empty() && url_.back() == '/') {
        url_.pop_back();
    }
}

Package NpmRepository::exact(const std::string& name, const std::string& version) {
    const auto endpoint = url_ + '/' + encode_component(name) + '/' + encode_component(version);
    return parse_package(parse_json(fetch_(endpoint)), name, version);
}

TestRepository::TestRepository(const std::string& filename) {
    const auto document = parse_json(read_test_file(filename));
    for (const auto& [name, value] : document.as_object()) {
        validate_test_name(name);
        auto& dependencies = packages_[name];
        for (const auto& dependency : value.as_array()) {
            validate_test_name(dependency.as_string());
            dependencies.push_back(dependency.as_string());
        }
    }
    for (const auto& [name, dependencies] : packages_) {
        for (const auto& dependency : dependencies) {
            if (packages_.count(dependency) == 0) {
                throw std::runtime_error("Тестовый репозиторий не содержит пакет " + dependency);
            }
        }
    }
}

Package TestRepository::exact(const std::string& name, const std::string& version) const {
    const auto found = packages_.find(name);
    if (found == packages_.end()) {
        throw std::runtime_error("В тестовом репозитории нет пакета " + name);
    }
    Package result{name, version, {}};
    for (const auto& dependency : found->second) {
        result.dependencies[dependency] = version;
    }
    return result;
}

void print_dependencies(const Package& package, std::ostream& output) {
    output << "package=" << package.name << '@' << package.version << '\n';
    output << "direct_dependencies=" << package.dependencies.size() << '\n';
    for (const auto& [name, range] : package.dependencies) {
        output << name << '=' << range << '\n';
    }
}

}
