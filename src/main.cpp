#include "config.hpp"

#include <exception>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int run(const std::vector<std::string>& arguments) {
    try {
        const auto config = graph_tool::parse_arguments(arguments);
        if (config.help) {
            graph_tool::print_help(std::cout);
        } else {
            graph_tool::print_config(config, std::cout);
        }
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
