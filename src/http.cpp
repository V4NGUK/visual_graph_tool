#include "http.hpp"

#include <array>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace graph_tool {

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

}
