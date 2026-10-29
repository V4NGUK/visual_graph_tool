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

void config_cli_tests(TestSuite& suite, const Cli& app) {
    suite.run("default CLI configuration", [&] {
        const auto result = app.run({"--package", "express", "--version", "4.18.2"});
        success(result);
        check(result.output == "package=express\nrepository=https://registry.npmjs.org\n"
            "test_mode=false\nversion=4.18.2\noutput=graph.svg\nascii=false\nfilter=\n", "Wrong configuration");
    });
    suite.run("CLI flags and Unicode paths", [&] {
        const auto output = app.directory / fs::u8path("граф с пробелами.svg");
        const auto result = app.run({"--package", "A", "--version", "1.0.0", "--test-mode",
            "--repository", (app.source / "examples/repository.json").u8string(),
            "--ascii", "--filter", "B", "--output", output.u8string()});
        contains(result, "test_mode=true");
        contains(result, "ascii=true");
        contains(result, output.u8string());
        check(!fs::exists(output), "Unexpected SVG output");
    });
    suite.run("CLI argument error", [&] {
        failure(app.run({"--package", "express", "--version", "invalid"}), "--version");
    });
    suite.run("CLI help", [&] {
        contains(app.run({"--help"}), "--package");
    });
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        check(argc == 3, "Expected executable and source paths");
        Cli app(argv[1], argv[2]);
        TestSuite suite;
        config_cli_tests(suite, app);
        return suite.finish();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
