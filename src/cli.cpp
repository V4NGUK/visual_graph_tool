#include "config.hpp"

#include <ostream>
#include <set>
#include <stdexcept>

namespace graph_tool {
namespace {

std::string read_value(const std::vector<std::string>& arguments, std::size_t& index) {
    const auto& argument = arguments[index];
    const auto separator = argument.find('=');
    if (separator != std::string::npos) {
        return argument.substr(separator + 1);
    }
    if (index + 1 >= arguments.size() ||
        arguments[index + 1].rfind("--", 0) == 0 ||
        arguments[index + 1] == "-h") {
        throw std::invalid_argument(argument + ": не указано значение");
    }
    ++index;
    return arguments[index];
}

bool set_flag(Config& config, const std::string& name) {
    if (name == "--test-mode") {
        config.test_mode = true;
    } else if (name == "--ascii") {
        config.ascii = true;
    } else if (name == "--help" || name == "-h") {
        config.help = true;
    } else if (name == "--direct") {
        config.direct = true;
    } else {
        return false;
    }
    return true;
}

void set_value(Config& config, const std::string& name,
               const std::vector<std::string>& arguments, std::size_t& index) {
    if (name == "--package") {
        config.package = read_value(arguments, index);
    } else if (name == "--repository") {
        config.repository = read_value(arguments, index);
    } else if (name == "--version") {
        config.version = read_value(arguments, index);
    } else if (name == "--output") {
        config.output = read_value(arguments, index);
    } else if (name == "--filter") {
        config.filter = read_value(arguments, index);
    } else {
        throw std::invalid_argument("неизвестный параметр: " + name);
    }
}

}

Config parse_arguments(const std::vector<std::string>& arguments) {
    Config config;
    std::set<std::string> seen;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto option = arguments[index].substr(0, arguments[index].find('='));
        if (seen.count(option) != 0) {
            throw std::invalid_argument(option + ": параметр указан повторно");
        }
        seen.insert(option);
        if (set_flag(config, option)) {
            if (arguments[index] != option) {
                throw std::invalid_argument(option + ": флаг не принимает значение");
            }
        } else {
            set_value(config, option, arguments, index);
        }
    }
    if (!config.help) {
        validate_config(config);
    }
    return config;
}

void print_config(const Config& config, std::ostream& output) {
    output << "package=" << config.package << '\n'
           << "repository=" << config.repository << '\n'
           << "test_mode=" << (config.test_mode ? "true" : "false") << '\n'
           << "version=" << config.version << '\n'
           << "output=" << config.output << '\n'
           << "ascii=" << (config.ascii ? "true" : "false") << '\n'
           << "filter=" << config.filter << '\n';
}

void print_help(std::ostream& output) {
    output << R"(Инструмент визуализации графа зависимостей npm
Вариант 8. Этапы 2 и 3: прямые зависимости и рекурсивный BFS.

Запуск: visual_graph_tool --package ИМЯ --version ВЕРСИЯ [опции]

  --package ИМЯ       Имя пакета npm; в тестовом режиме: A, B, ABC.
  --repository ПУТЬ   URL HTTP(S) или файл при --test-mode.
                     По умолчанию: https://registry.npmjs.org
  --test-mode         Включить режим тестового репозитория.
  --version ВЕРСИЯ    Точная версия SemVer, например 1.2.3.
  --output ФАЙЛ       Имя изображения SVG; по умолчанию: graph.svg.
  --ascii             Включить режим ASCII-дерева.
  --filter ПОДСТРОКА  Подстрока исключаемых пакетов; по умолчанию пустая.
  --direct            Вывести только прямые зависимости, как на этапе 2.
  --help, -h          Показать справку.

Значения можно задавать через пробел или =. Флаги значений не принимают.
По умолчанию выводятся граф и порядок BFS. --direct включает вывод этапа 2.
Обрабатывается поле dependencies; devDependencies и peerDependencies не обходятся.
Изображение и ASCII-дерево появятся на этапе 5.
)";
}

}
