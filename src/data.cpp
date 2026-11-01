#include "app.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace graph_tool {

namespace {

constexpr std::size_t max_json_depth = 128;

void append_utf8(std::string& result, std::uint32_t value) {
    if (value <= 0x7f) {
        result += static_cast<char>(value);
    } else if (value <= 0x7ff) {
        result += static_cast<char>(0xc0 | (value >> 6));
        result += static_cast<char>(0x80 | (value & 0x3f));
    } else if (value <= 0xffff) {
        result += static_cast<char>(0xe0 | (value >> 12));
        result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (value & 0x3f));
    } else {
        result += static_cast<char>(0xf0 | (value >> 18));
        result += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
        result += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        result += static_cast<char>(0x80 | (value & 0x3f));
    }
}

class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    Json parse() {
        if (input_.compare(0, 3, "\xef\xbb\xbf") == 0) {
            position_ = 3;
        }
        auto result = parse_value(0);
        skip_spaces();
        if (position_ != input_.size()) {
            fail("лишние символы после значения");
        }
        return result;
    }

private:
    const std::string& input_;
    std::size_t position_ = 0;

    [[noreturn]] void fail(const std::string& reason) const {
        throw std::runtime_error("JSON, позиция " + std::to_string(position_) + ": " + reason);
    }

    void skip_spaces() {
        while (position_ < input_.size() &&
               std::string(" \t\r\n").find(input_[position_]) != std::string::npos) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skip_spaces();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            fail(std::string("ожидается символ ") + expected);
        }
    }

    std::uint32_t read_hex_code() {
        std::uint32_t result = 0;
        for (int index = 0; index < 4; ++index) {
            if (position_ == input_.size()) {
                fail("незавершённый Unicode escape");
            }
            const auto digit = input_[position_++];
            const auto found = std::string("0123456789abcdef").find(
                digit >= 'A' && digit <= 'F' ? static_cast<char>(digit - 'A' + 'a') : digit);
            if (found == std::string::npos) {
                fail("некорректный Unicode escape");
            }
            result = result * 16 + static_cast<std::uint32_t>(found);
        }
        return result;
    }

    void read_unicode(std::string& result) {
        auto code = read_hex_code();
        if (code >= 0xd800 && code <= 0xdbff) {
            if (input_.compare(position_, 2, "\\u") != 0) {
                fail("нет второй части суррогатной пары");
            }
            position_ += 2;
            const auto low = read_hex_code();
            if (low < 0xdc00 || low > 0xdfff) {
                fail("неверная суррогатная пара");
            }
            code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            fail("непарный суррогат");
        }
        append_utf8(result, code);
    }

    void read_escape(std::string& result) {
        if (position_ == input_.size()) {
            fail("незавершённая escape-последовательность");
        }
        const auto character = input_[position_++];
        if (character == 'u') {
            read_unicode(result);
            return;
        }
        const std::string keys = "\"\\/bfnrt";
        const std::string values = "\"\\/\b\f\n\r\t";
        const auto index = keys.find(character);
        if (index == std::string::npos) {
            fail("неизвестная escape-последовательность");
        }
        result += values[index];
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const auto character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return result;
            }
            if (character < 0x20) {
                fail("управляющий символ в строке");
            }
            if (character == '\\') {
                read_escape(result);
            } else {
                result += static_cast<char>(character);
            }
        }
        fail("незавершённая строка");
    }

    Json parse_object(std::size_t depth) {
        expect('{');
        Json result;
        result.kind = Json::Kind::object;
        if (consume('}')) {
            return result;
        }
        do {
            const auto key = parse_string();
            expect(':');
            if (result.members.count(key) != 0) {
                fail("повторяющийся ключ: " + key);
            }
            result.members[key] = parse_value(depth + 1);
        } while (consume(','));
        expect('}');
        return result;
    }

    Json parse_array(std::size_t depth) {
        expect('[');
        Json result;
        result.kind = Json::Kind::array;
        if (consume(']')) {
            return result;
        }
        do {
            result.items.push_back(parse_value(depth + 1));
        } while (consume(','));
        expect(']');
        return result;
    }

    Json parse_primitive() {
        const auto start = position_;
        while (position_ < input_.size() &&
               std::string(" \t\r\n,]}").find(input_[position_]) == std::string::npos) {
            ++position_;
        }
        Json result;
        result.text = input_.substr(start, position_ - start);
        if (result.text == "null") {
            return result;
        }
        if (result.text == "true" || result.text == "false") {
            result.kind = Json::Kind::boolean;
            return result;
        }
        static const std::regex number(R"(-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
        if (!std::regex_match(result.text, number)) {
            fail("неверное значение");
        }
        result.kind = Json::Kind::number;
        return result;
    }

    Json parse_value(std::size_t depth) {
        skip_spaces();
        if (depth > max_json_depth || position_ == input_.size()) {
            fail("слишком большая вложенность или неожиданный конец");
        }
        switch (input_[position_]) {
        case '{': return parse_object(depth);
        case '[': return parse_array(depth);
        case '"': {
            Json result;
            result.kind = Json::Kind::string;
            result.text = parse_string();
            return result;
        }
        default: return parse_primitive();
        }
    }
};

}

const std::string& Json::as_string() const {
    if (kind != Kind::string) {
        throw std::runtime_error("JSON: ожидается строка");
    }
    return text;
}

const std::vector<Json>& Json::as_array() const {
    if (kind != Kind::array) {
        throw std::runtime_error("JSON: ожидается массив");
    }
    return items;
}

const std::map<std::string, Json>& Json::as_object() const {
    if (kind != Kind::object) {
        throw std::runtime_error("JSON: ожидается объект");
    }
    return members;
}

const Json* Json::find(const std::string& key) const {
    const auto& object = as_object();
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}

Json parse_json(const std::string& input) {
    return Parser(input).parse();
}

std::string encode_component(const std::string& value) {
    const std::string hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            std::string("-._~").find(character) != std::string::npos) {
            result += static_cast<char>(character);
        } else {
            result += '%';
            result += hex[character >> 4];
            result += hex[character & 15];
        }
    }
    return result;
}

#ifdef _WIN32
namespace {

constexpr std::size_t max_response_bytes = 32 * 1024 * 1024;
constexpr int timeout_ms = 15000;

struct CloseHttp {
    void operator()(HINTERNET value) const {
        WinHttpCloseHandle(value);
    }
};

using Handle = std::unique_ptr<void, CloseHttp>;

void check_http(bool success, const std::string& action) {
    if (!success) {
        throw std::runtime_error(action + ": ошибка Windows " + std::to_string(GetLastError()));
    }
}

Handle handle(HINTERNET value) {
    check_http(value != nullptr, "HTTP");
    return Handle(value);
}

std::wstring wide(const std::string& value) {
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    check_http(length > 0, "Некорректный URL UTF-8");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    check_http(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
                                  result.data(), length) > 0, "Преобразование URL");
    result.pop_back();
    return result;
}

std::string read_response(HINTERNET request) {
    std::string result;
    std::array<char, 16384> buffer{};
    DWORD received = 0;
    do {
        check_http(WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()),
                                   &received), "Чтение ответа HTTP");
        if (result.size() + received > max_response_bytes) {
            throw std::runtime_error("Ответ реестра превышает 32 МБ");
        }
        result.append(buffer.data(), received);
    } while (received != 0);
    return result;
}

void send_request(HINTERNET request, const std::string& url) {
    const wchar_t* headers = L"Accept: application/vnd.npm.install-v1+json, application/json\r\n";
    check_http(WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                                 nullptr, 0, 0, 0), "Отправка запроса " + url);
    check_http(WinHttpReceiveResponse(request, nullptr), "Получение ответа " + url);
    DWORD status = 0;
    DWORD size = sizeof(status);
    check_http(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  nullptr, &status, &size, nullptr), "Проверка статуса HTTP");
    if (status != 200) {
        throw std::runtime_error("HTTP " + std::to_string(status) + ": " + url);
    }
}

}

std::string http_get(const std::string& url) {
    const auto address = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    check_http(WinHttpCrackUrl(address.c_str(), 0, 0, &parts), "Разбор URL");
    const std::wstring hostname(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    auto session = handle(WinHttpOpen(L"visual-graph-tool/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     nullptr, nullptr, 0));
    check_http(WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms),
               "Настройка таймаута HTTP");
    auto connection = handle(WinHttpConnect(session.get(), hostname.c_str(), parts.nPort, 0));
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    auto request = handle(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                            nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    send_request(request.get(), url);
    return read_response(request.get());
}
#else
std::string http_get(const std::string&) {
    throw std::runtime_error("Сетевой режим этой сборки требует Windows и WinHTTP");
}
#endif

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
