#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;
using Arguments = std::vector<std::string>;

struct Handle {
    HANDLE value;

    explicit Handle(HANDLE handle) : value(handle) {
        check(value != nullptr && value != INVALID_HANDLE_VALUE, "Cannot create Windows handle");
    }

    ~Handle() {
        CloseHandle(value);
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring wide(const std::string& text) {
    return fs::u8path(text).wstring();
}

std::wstring quote(const std::wstring& text) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t character : text) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += character;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

struct Result {
    int code;
    std::string output;
    std::string error;
};

class Cli {
public:
    fs::path executable;
    fs::path source;
    fs::path directory;

    Cli(const fs::path& app, const fs::path& root) : executable(fs::absolute(app)), source(fs::absolute(root)) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = executable.parent_path() / ("cli-test-" + std::to_string(stamp));
        check(fs::create_directory(directory), "Cannot create test directory");
    }

    ~Cli() {
        std::error_code error;
        for (fs::directory_iterator item(directory, error), end; item != end; item.increment(error)) {
            if (error) break;
            fs::remove(item->path(), error);
        }
        fs::remove(directory, error);
    }

    Result run(const Arguments& arguments) const {
        std::wstring command = quote(executable.wstring());
        for (const auto& argument : arguments) {
            command += L' ' + quote(wide(argument));
        }
        const int code = execute(command);
        auto output = read_file(directory / "stdout.txt");
        auto error = read_file(directory / "stderr.txt");
        output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
        error.erase(std::remove(error.begin(), error.end(), '\r'), error.end());
        return {code, output, error};
    }

    fs::path write(const std::string& name, const std::string& text) const {
        const auto file = directory / fs::u8path(name);
        std::ofstream output(file, std::ios::binary);
        output << text;
        check(output.good(), "Cannot write test data");
        return file;
    }

private:
    int execute(std::wstring command) const {
        SECURITY_ATTRIBUTES access{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle output(CreateFileW((directory / "stdout.txt").c_str(), GENERIC_WRITE,
                                  FILE_SHARE_READ, &access, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle error(CreateFileW((directory / "stderr.txt").c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, &access, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &access, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.value;
        startup.hStdOutput = output.value;
        startup.hStdError = error.value;
        PROCESS_INFORMATION process{};
        check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process), "Cannot launch CLI");
        Handle process_handle(process.hProcess);
        Handle thread_handle(process.hThread);
        const auto wait = WaitForSingleObject(process_handle.value, 30000);
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(process_handle.value, 1);
            WaitForSingleObject(process_handle.value, 5000);
            throw std::runtime_error("CLI did not finish within 30 seconds");
        }
        DWORD code = 0;
        check(GetExitCodeProcess(process_handle.value, &code), "Cannot get CLI exit code");
        return static_cast<int>(code);
    }
};

void success(const Result& result) {
    check(result.code == 0 && result.error.empty(), "Unexpected CLI error: " + result.error);
}

void failure(const Result& result, const std::string& message = "") {
    check(result.code == 2 && result.output.empty() && !result.error.empty(), "Expected CLI failure");
    check(result.error.find(message) != std::string::npos, "Missing error detail: " + message);
}

void contains(const Result& result, const std::string& text) {
    success(result);
    check(result.output.find(text) != std::string::npos, "Missing output: " + text);
}

#include <atomic>
#include <sstream>

struct Winsock {
    Winsock() {
        WSADATA data{};
        check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Cannot start Winsock");
    }

    ~Winsock() {
        WSACleanup();
    }
};

struct Socket {
    SOCKET value;

    explicit Socket(SOCKET socket) : value(socket) {
        check(value != INVALID_SOCKET, "Cannot create socket");
    }

    ~Socket() {
        closesocket(value);
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

class Registry {
public:
    Registry() : listener_(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(bind(listener_.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "Bind failed");
        int length = sizeof(address);
        check(getsockname(listener_.value, reinterpret_cast<sockaddr*>(&address), &length) == 0, "Port not found");
        port_ = ntohs(address.sin_port);
        check(listen(listener_.value, SOMAXCONN) == 0, "Listen failed");
        saved_packages();
        worker_ = std::thread([this] { serve(); });
    }

    ~Registry() {
        running_ = false;
        worker_.join();
    }

    std::string url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

private:
    Winsock winsock_;
    Socket listener_;
    unsigned short port_ = 0;
    std::atomic<bool> running_{true};
    std::thread worker_;

    std::string decode(std::string name) const {
        std::size_t position = 0;
        while ((position = name.find('%', position)) != std::string::npos) {
            check(position + 2 < name.size(), "Invalid URL escape");
            const char character = static_cast<char>(std::stoi(name.substr(position + 1, 2), nullptr, 16));
            name.replace(position, 3, 1, character);
            ++position;
        }
        return name;
    }

    std::pair<int, std::string> response(std::string path) const {
        std::string mode;
        path.erase(0, 1);
        for (const std::string prefix : {"error", "broken", "wrong", "npm"}) {
            if (path.rfind(prefix + '/', 0) == 0) {
                mode = prefix;
                path.erase(0, prefix.size() + 1);
                break;
            }
        }
        if (mode == "error") return {503, "{}"};
        const auto slash = path.find('/');
        const auto name = decode(path.substr(0, slash));
        const auto metadata = saved_packages().find(name);
        if (!metadata) return {404, "{}"};
        if (slash == std::string::npos) return {200, metadata_versions(name)};
        const auto version = decode(path.substr(slash + 1));
        if (version != metadata->as_object().at("version").as_string()) return {404, "{}"};
        if (mode == "broken") return {200, "{"};
        if (mode == "wrong") {
            auto document = *metadata;
            document.members.at("version").text = "999.0.0";
            return {200, json_text(document)};
        }
        return {200, metadata_text(name)};
    }

    void answer(SOCKET client) const {
        DWORD timeout = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) return;
            request.append(buffer, received);
        }
        std::istringstream input(request);
        std::string method, path;
        input >> method >> path;
        if (method != "GET" || path.empty()) return;
        const auto [status, body] = response(path);
        const auto reply = "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Type: application/json\r\n"
            "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        std::size_t sent = 0;
        while (sent < reply.size()) {
            const int count = send(client, reply.data() + sent, static_cast<int>(reply.size() - sent), 0);
            if (count <= 0) return;
            sent += count;
        }
    }

    void serve() {
        while (running_) {
            fd_set sockets;
            FD_ZERO(&sockets);
            FD_SET(listener_.value, &sockets);
            timeval timeout{0, 100000};
            if (select(0, &sockets, nullptr, nullptr, &timeout) <= 0) continue;
            const auto accepted = accept(listener_.value, nullptr, nullptr);
            if (accepted == INVALID_SOCKET) continue;
            Socket client(accepted);
            try {
                answer(client.value);
            } catch (const std::exception&) {
                shutdown(client.value, SD_BOTH);
            }
        }
    }
};

std::string unavailable_url(Socket& reserved) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(bind(reserved.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "Bind failed");
    int length = sizeof(address);
    check(getsockname(reserved.value, reinterpret_cast<sockaddr*>(&address), &length) == 0, "Port not found");
    return "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
}

Result npm(const Cli& app, const std::string& url, const std::string& name = "chalk",
           const std::string& version = "4.1.2", const std::string& suffix = "") {
    Arguments arguments = {"--package", name, "--version", version};
    arguments.insert(arguments.end(), {"--repository", url + suffix, "--direct"});
    return app.run(arguments);
}

void data_cli_tests(TestSuite& suite, const Cli& app, const std::string& url) {
    suite.run("CLI direct dependencies", [&] {
        const auto result = npm(app, url);
        success(result);
        check(result.output == "package=chalk@4.1.2\ndirect_dependencies=2\n"
            "ansi-styles=^4.1.0\nsupports-color=^7.1.0\n", "Wrong direct dependencies");
    });
    suite.run("CLI leaf package", [&] {
        contains(npm(app, url, "is-number", "7.0.0"), "direct_dependencies=0");
    });
    suite.run("CLI scoped package", [&] {
        contains(npm(app, url, "@types/is-number", "7.0.5"), "package=@types/is-number@7.0.5");
    });
    suite.run("CLI missing package and version", [&] {
        failure(npm(app, url, "absent", "1.0.0"), "HTTP 404");
        failure(npm(app, url, "chalk", "999.0.0"), "HTTP 404");
    });
    suite.run("CLI server errors", [&] {
        for (const auto suffix : {"/broken", "/wrong", "/error"}) {
            failure(npm(app, url, "chalk", "4.1.2", suffix));
        }
    });
    suite.run("CLI local repository and Unicode path", [&] {
        const auto file = app.write("граф с пробелами.json", R"({"A":["B"],"B":[]})");
        const auto result = app.run({"--package", "A", "--version", "1.0.0",
                                    "--test-mode", "--repository", file.u8string(), "--direct"});
        contains(result, "B=1.0.0");
    });
    suite.run("CLI invalid repositories", [&] {
        for (const auto text : {"{", R"({"A":["B"]})", R"({"a":[]})", R"({"A":{}})"}) {
            const auto file = app.write("invalid.json", text);
            failure(app.run({"--package", "A", "--version", "1.0.0",
                             "--test-mode", "--repository", file.u8string()}));
        }
    });
    suite.run("CLI help and validation", [&] {
        contains(app.run({"--help"}), "--package");
        failure(app.run({"--package", "chalk", "--version", "invalid"}), "--version");
    });
}

Result graph(const Cli& app, const std::string& package, const Arguments& extra = {}) {
    Arguments arguments = {"--package", package, "--version", "1.0.0", "--test-mode", "--repository",
                           (app.source / "examples/repository.json").u8string()};
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    return app.run(arguments);
}

void graph_cli_tests(TestSuite& suite, const Cli& app, const std::string& url) {
    suite.run("CLI unavailable registry", [&] {
        Socket reserved(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        failure(app.run({"--package", "chalk", "--version", "4.1.2", "--repository", unavailable_url(reserved)}));
    });
    suite.run("CLI BFS and shared dependencies", [&] {
        const auto result = graph(app, "A");
        contains(result, "nodes=4\nedges=4\n");
        contains(result, "bfs=A@1.0.0,B@1.0.0,C@1.0.0,D@1.0.0");
    });
    suite.run("CLI cycles, loops and isolated packages", [&] {
        contains(graph(app, "X"), "nodes=3\nedges=3\n");
        contains(graph(app, "S"), "nodes=1\nedges=1\n");
        contains(graph(app, "I"), "nodes=1\nedges=0\n");
    });
    suite.run("CLI filter", [&] {
        const auto result = graph(app, "A", {"--filter", "B"});
        contains(result, "nodes=3\nedges=2\n");
        contains(result, "D@1.0.0");
        check(result.output.find("B@1.0.0") == std::string::npos, "Filtered package remained");
        failure(app.run({"--direct=false"}), "--direct");
    });
    suite.run("CLI filtered root does not contact registry", [&] {
        const auto result = app.run({"--package", "absent", "--version", "1.0.0",
                                     "--repository", url, "--filter", "abs"});
        contains(result, "nodes=0\nedges=0\n");
    });
    suite.run("CLI full npm graph", [&] {
        const auto result = app.run({"--package", "chalk", "--version", "4.1.2", "--repository", url});
        contains(result, "nodes=6\nedges=5\n");
        contains(result, "color-name@1.1.4 -> -");
    });
    suite.run("CLI stage 5 output is not created", [&] {
        const auto output = app.directory / fs::u8path("граф.svg");
        success(graph(app, "X", {"--ascii", "--output", output.u8string()}));
        check(!fs::exists(output), "Unexpected SVG output");
    });
}

void reverse_cli_tests(TestSuite& suite, const Cli& app, const std::string& url) {
    suite.run("CLI reverse diamond", [&] {
        const auto result = graph(app, "A", {"--reverse", "D"});
        success(result);
        check(result.output == "package=D@1.0.0\nreverse_dependencies=3\nB@1.0.0\nC@1.0.0\nA@1.0.0\n",
              "Wrong reverse dependencies");
    });
    suite.run("CLI reverse cycle", [&] {
        const auto result = graph(app, "X", {"--reverse=X"});
        success(result);
        check(result.output == "package=X@1.0.0\nreverse_dependencies=2\nZ@1.0.0\nY@1.0.0\n", "Wrong cycle");
    });
    suite.run("CLI reverse leaf, root, isolated package and self loop", [&] {
        contains(graph(app, "A", {"--reverse", "B"}), "reverse_dependencies=1\nA@1.0.0\n");
        for (const auto name : {"A", "I", "S"}) {
            contains(graph(app, name, {"--reverse", name}), "reverse_dependencies=0\n");
        }
    });
    suite.run("CLI reverse filter and exact target", [&] {
        contains(graph(app, "A", {"--reverse", "D@1.0.0", "--filter", "B"}),
                 "reverse_dependencies=2\nC@1.0.0\nA@1.0.0\n");
        failure(graph(app, "A", {"--reverse", "D", "--filter", "D"}), "--reverse");
        failure(graph(app, "A", {"--reverse", "D", "--filter", "A"}), "--reverse");
    });
    suite.run("CLI absent reverse targets", [&] {
        for (const auto name : {"MISSING", "I", "D@2.0.0"}) {
            failure(graph(app, "A", {"--reverse", name}), "--reverse");
        }
    });
    suite.run("CLI invalid reverse options", [&] {
        failure(graph(app, "A", {"--reverse"}), "--reverse");
        failure(graph(app, "A", {"--reverse="}), "--reverse");
        failure(graph(app, "A", {"--reverse", "d"}), "--reverse");
        failure(graph(app, "A", {"--reverse", "D@latest"}), "--reverse");
        failure(graph(app, "A", {"--reverse", "D", "--direct"}), "--reverse");
        failure(graph(app, "A", {"--reverse=D", "--reverse=B"}), "--reverse");
    });
    suite.run("CLI reverse npm dependencies over HTTP", [&] {
        const auto result = app.run({"--package", "chalk", "--version", "4.1.2", "--repository", url,
                                     "--reverse", "color-name"});
        success(result);
        check(result.output == "package=color-name@1.1.4\nreverse_dependencies=3\n"
              "color-convert@2.0.1\nansi-styles@4.3.0\nchalk@4.1.2\n", "Wrong npm reverse graph");
    });
    suite.run("CLI reverse scoped target", [&] {
        contains(app.run({"--package", "@types/is-number", "--version", "7.0.5", "--repository", url,
                          "--reverse", "@types/is-number@7.0.5"}), "reverse_dependencies=0\n");
    });
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        check(argc == 3, "Expected executable and source paths");
        Cli app(argv[1], argv[2]);
        TestSuite suite;
        Registry registry;
        data_cli_tests(suite, app, registry.url());
        graph_cli_tests(suite, app, registry.url());
        reverse_cli_tests(suite, app, registry.url());
        return suite.finish();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
