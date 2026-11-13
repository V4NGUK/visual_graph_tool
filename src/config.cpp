#include "app.hpp"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <regex>
#include <set>
#include <stdexcept>

namespace graph_tool {

namespace {

constexpr std::size_t max_package_length = 214;
constexpr unsigned long max_port = 65535;
constexpr unsigned char space_character = 32;
constexpr unsigned char delete_character = 127;

bool contains_control(const std::string& value, bool include_space) {
    for (unsigned char character : value) {
        if (character < space_character || character == delete_character) {
            return true;
        }
        if (include_space && character == space_character) {
            return true;
        }
    }
    return false;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void validate_package(const Config& config) {
    require(!config.package.empty(), "--package: необходимо указать имя пакета");
    require(config.package.size() <= max_package_length,
            "--package: имя не должно превышать 214 символов");
    const std::regex pattern(config.test_mode
        ? R"([A-Z]+)"
        : R"(([a-z0-9-][a-z0-9._-]*|@[a-z0-9._-]+/[a-z0-9._-]+))");
    require(std::regex_match(config.package, pattern), config.test_mode
        ? "--package: в тестовом режиме нужны заглавные латинские буквы"
        : "--package: ожидается имя npm, например express или @types/node");
}

void validate_version(const std::string& version) {
    const std::string number = R"((0|[1-9][0-9]*))";
    const std::string identifier =
        R"((0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))";
    const std::regex pattern(number + "\\." + number + "\\." + number +
        "(-" + identifier + "(\\." + identifier + ")*)?" +
        R"((\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?)");
    require(std::regex_match(version, pattern),
            "--version: нужна точная версия SemVer, например 1.2.3 или 1.2.3-beta.1");
}

void validate_url(const std::string& repository) {
    const std::string label = R"([A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)";
    const std::regex pattern("https?://" + label + "(?:\\." + label + ")*"
        R"((?::([0-9]{1,5}))?(?:/[A-Za-z0-9._~!$&'()*+,;=:@%/-]*)?)");
    std::smatch match;
    require(std::regex_match(repository, match, pattern),
            "--repository: нужен URL http:// или https:// с корректным именем узла");
    if (match[1].matched) {
        const auto port = std::stoul(match[1]);
        require(port > 0 && port <= max_port,
                "--repository: номер порта должен находиться в диапазоне 1-65535");
    }
    const std::regex invalid_escape(R"(%($|[^0-9A-Fa-f]|[0-9A-Fa-f]($|[^0-9A-Fa-f])))");
    require(!std::regex_search(repository, invalid_escape),
            "--repository: некорректное процентное кодирование URL");
}

void validate_test_file(const std::string& repository) {
    const auto path = std::filesystem::u8path(repository);
    std::error_code error;
    require(std::filesystem::is_regular_file(path, error) && !error,
            "--repository: тестовый репозиторий должен быть существующим файлом");
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "--repository: невозможно прочитать тестовый файл");
    require(input.peek() != std::ifstream::traits_type::eof(),
            "--repository: тестовый файл пуст или недоступен для чтения");
}

void validate_repository(const Config& config) {
    require(!config.repository.empty(), "--repository: значение не должно быть пустым");
    require(config.repository.find('\0') == std::string::npos,
            "--repository: недопустимый нулевой символ");
    if (config.test_mode) {
        validate_test_file(config.repository);
    } else {
        validate_url(config.repository);
    }
}

void validate_filename(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    require(!name.empty() && !contains_control(name, false) && name.find_first_of("<>:\"|?*") == std::string::npos,
            "--output: имя файла содержит недопустимые символы");
    const std::regex reserved(R"((CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?)",
                              std::regex::icase);
    require(!std::regex_match(name, reserved), "--output: зарезервированное имя файла");
    require(path.extension() == ".svg", "--output: необходимо расширение .svg");
}

void validate_output(const std::string& output) {
    require(!output.empty(), "--output: необходимо указать имя SVG-файла");
    require(output.find('\0') == std::string::npos, "--output: недопустимый нулевой символ");
    const auto path = std::filesystem::u8path(output);
    validate_filename(path);
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::error_code error;
    require(std::filesystem::is_directory(parent, error) && !error,
            "--output: родительский каталог должен существовать");
    const bool exists = std::filesystem::exists(path, error);
    require(!error, "--output: невозможно проверить указанный путь");
    if (exists) {
        require(std::filesystem::is_regular_file(path, error) && !error,
                "--output: путь должен указывать на файл, а не на каталог");
    }
}

void validate_reverse(const Config& config) {
    if (config.reverse.empty()) return;
    require(!config.direct, "--reverse: нельзя использовать вместе с --direct");
    Config target = config;
    target.package = config.reverse;
    const auto separator = config.reverse.rfind('@');
    if (separator != std::string::npos && separator != 0) {
        target.package = config.reverse.substr(0, separator);
        target.version = config.reverse.substr(separator + 1);
    }
    try {
        validate_package(target);
        validate_version(target.version);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("--reverse: неверное имя или точная версия пакета");
    }
}

}

void validate_config(const Config& config) {
    validate_package(config);
    validate_version(config.version);
    validate_repository(config);
    validate_output(config.output);
    require(!contains_control(config.filter, true),
            "--filter: подстрока не должна содержать пробелы и управляющие символы");
    require(config.filter.size() <= max_package_length,
            "--filter: подстрока не должна превышать 214 байт");
    validate_reverse(config);
}

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
    } else if (name == "--reverse") {
        config.reverse = read_value(arguments, index);
        require(!config.reverse.empty(), "--reverse: необходимо указать пакет");
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
Вариант 8. Этапы 1–5: зависимости, BFS, обратные зависимости и визуализация.

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
  --reverse ПАКЕТ     Обратные зависимости пакета в построенном графе.
                     Можно уточнить версию: имя@версия. Несовместим с --direct.
  --help, -h          Показать справку.

Значения можно задавать через пробел или =. Флаги значений не принимают.
По умолчанию выводятся граф и порядок BFS. --direct включает вывод этапа 2.
--reverse выводит прямые и косвенные обратные зависимости в порядке BFS, без самого пакета.
Обрабатывается поле dependencies; devDependencies и peerDependencies не обходятся.
Полный граф сохраняется в SVG и соседний DOT-файл. Нужен Graphviz (dot).
--ascii заменяет список рёбер деревом; [cycle] означает цикл, [seen] — повтор.
Режимы --direct и --reverse только печатают результат, без SVG и ASCII-дерева.
)";
}

}
