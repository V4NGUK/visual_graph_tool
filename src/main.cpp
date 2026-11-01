#include "config.hpp"
#include "repository.hpp"
#include "graph.hpp"

#include <exception>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace graph_tool;

void analyze_test_file(const Config& config) {
    TestRepository repository(config.repository);
    const auto root = repository.exact(config.package, config.version);
    if (config.direct) {
        print_dependencies(root, std::cout);
    } else {
        const auto resolve = [&](const std::string& name, const std::string& version) {
            return repository.exact(name, version);
        };
        print_graph(build_graph(root, resolve, config.filter), std::cout);
    }
}

void analyze_npm(const Config& config) {
    NpmRepository repository(config.repository);
    const auto root = repository.exact(config.package, config.version);
    if (config.direct) {
        print_dependencies(root, std::cout);
    } else {
        const auto resolve = [&](const std::string& name, const std::string& range) {
            return repository.resolve(name, range);
        };
        print_graph(build_graph(root, resolve, config.filter), std::cout);
    }
}

void process(const Config& config) {
    if (config.help) {
        print_help(std::cout);
    } else if (!config.direct && excluded(config.package, config.filter)) {
        print_graph({config.package + '@' + config.version, {}, {}}, std::cout);
    } else if (config.test_mode) {
        analyze_test_file(config);
    } else {
        analyze_npm(config);
    }
}

int run(const std::vector<std::string>& arguments) {
    try {
        process(parse_arguments(arguments));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n'
                  << "Используйте --help для просмотра справки.\n";
        return 2;
    }
}

#ifdef _WIN32
std::string to_utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
#endif

}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.push_back(to_utf8(argv[index]));
    }
    return run(arguments);
}
#else
int main(int argc, char* argv[]) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
