#include "app.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace graph_tool {
namespace {

namespace fs = std::filesystem;

std::string dot_label(const std::string& text) {
    std::string result = "\"";
    for (char character : text) {
        if (character == '\\' || character == '"') result += '\\';
        if (character == '\n') result += "\\n";
        else if (character == '\r') result += "\\r";
        else result += character;
    }
    return result + '"';
}

struct TreeLine {
    std::string id;
    std::string prefix;
    bool last;
    bool leave;
};

struct TemporaryFiles {
    fs::path directory;

    explicit TemporaryFiles(const fs::path& parent) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = parent / ("graphviz-" + std::to_string(stamp));
        if (!fs::create_directory(directory)) throw std::runtime_error("Не удалось создать временную папку");
    }

    ~TemporaryFiles() {
        std::error_code error;
        for (const auto* name : {"graph.dot", "graph.svg", "error.txt"}) {
            fs::remove(directory / name, error);
        }
        fs::remove(directory, error);
    }
};

#ifdef _WIN32
struct Handle {
    HANDLE value;

    explicit Handle(HANDLE handle) : value(handle) {
        if (value == INVALID_HANDLE_VALUE || value == nullptr) {
            throw std::runtime_error("Не удалось открыть файл для Graphviz");
        }
    }

    ~Handle() { CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring dot_program() {
    std::wstring path(32768, L'\0');
    auto size = GetEnvironmentVariableW(L"GRAPHVIZ_DOT", path.data(), static_cast<DWORD>(path.size()));
    if (size >= path.size()) throw std::runtime_error("Слишком длинный путь GRAPHVIZ_DOT");
    if (size > 0) {
        path.resize(size);
        return path;
    }
#ifdef GRAPHVIZ_DOT_PATH
    return fs::u8path(GRAPHVIZ_DOT_PATH).wstring();
#else
    size = SearchPathW(nullptr, L"dot.exe", nullptr, static_cast<DWORD>(path.size()), path.data(), nullptr);
    if (size == 0 || size >= path.size()) throw std::runtime_error("Установите Graphviz и добавьте dot.exe в PATH");
    path.resize(size);
    return path;
#endif
}

void render_svg(const fs::path& directory) {
    SECURITY_ATTRIBUTES access{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle input(CreateFileW((directory / "graph.dot").c_str(), GENERIC_READ, FILE_SHARE_READ,
                             &access, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle output(CreateFileW((directory / "graph.svg").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              &access, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle error(CreateFileW((directory / "error.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &access, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value;
    startup.hStdOutput = output.value;
    startup.hStdError = error.value;
    PROCESS_INFORMATION process{};
    const auto program = dot_program();
    std::wstring command = L"\"" + program + L"\" -Tsvg";
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw std::runtime_error("Не удалось запустить Graphviz. Проверьте dot.exe или GRAPHVIZ_DOT");
    }
    Handle process_handle(process.hProcess);
    Handle thread_handle(process.hThread);
    if (WaitForSingleObject(process_handle.value, 20000) != WAIT_OBJECT_0) {
        TerminateProcess(process_handle.value, 1);
        WaitForSingleObject(process_handle.value, INFINITE);
        throw std::runtime_error("Graphviz не завершился за 20 секунд");
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process_handle.value, &code) || code != 0) {
        std::ifstream log(directory / "error.txt", std::ios::binary);
        const std::string message((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
        throw std::runtime_error("Ошибка Graphviz: " + message.substr(0, 2000));
    }
}
#else
void render_svg(const fs::path&) {
    throw std::runtime_error("Запуск Graphviz поддерживается только в Windows");
}
#endif

}

void print_dot(const DependencyGraph& graph, std::ostream& output) {
    output << "digraph dependencies {\n"
           << "  graph [rankdir=LR, bgcolor=\"white\", pad=0.3];\n"
           << "  node [shape=box, style=\"rounded,filled\", fillcolor=\"#edf4ff\", fontname=\"Arial\"];\n"
           << "  edge [color=\"#53718e\"];\n";
    for (const auto& id : graph.order) {
        output << "  " << dot_label(id);
        if (id == graph.root) output << " [fillcolor=\"#b8dcff\", penwidth=2]";
        output << ";\n";
    }
    for (const auto& id : graph.order) {
        const auto found = graph.edges.find(id);
        if (found == graph.edges.end()) continue;
        for (const auto& child : found->second) {
            output << "  " << dot_label(id) << " -> " << dot_label(child) << ";\n";
        }
    }
    output << "}\n";
}

void print_ascii(const DependencyGraph& graph, std::ostream& output) {
    if (graph.order.empty()) {
        output << "(empty)\n";
        return;
    }
    std::set<std::string> active;
    std::set<std::string> seen;
    std::vector<TreeLine> pending{{graph.root, "", true, false}};
    while (!pending.empty()) {
        const auto line = pending.back();
        pending.pop_back();
        if (line.leave) {
            active.erase(line.id);
            continue;
        }
        const bool root = seen.empty();
        output << line.prefix << (root ? "" : line.last ? "`-- " : "|-- ") << line.id;
        if (active.count(line.id)) output << " [cycle]\n";
        else if (seen.count(line.id)) output << " [seen]\n";
        else {
            output << '\n';
            active.insert(line.id);
            seen.insert(line.id);
            pending.push_back({line.id, "", true, true});
            const auto found = graph.edges.find(line.id);
            if (found == graph.edges.end()) continue;
            const auto prefix = line.prefix + (root ? "" : line.last ? "    " : "|   ");
            const auto& children = found->second;
            for (std::size_t index = children.size(); index > 0; --index) {
                pending.push_back({children[index - 1], prefix, index == children.size(), false});
            }
        }
    }
}

void save_visualization(const DependencyGraph& graph, const std::string& filename) {
    const auto svg = fs::absolute(fs::u8path(filename));
    auto dot = svg;
    dot.replace_extension(".dot");
    if (fs::is_directory(dot)) throw std::runtime_error("Путь DOT указывает на папку");
    TemporaryFiles files(svg.parent_path());
    {
        std::ofstream output(files.directory / "graph.dot", std::ios::binary);
        print_dot(graph, output);
        output.close();
        if (!output) throw std::runtime_error("Не удалось записать описание Graphviz");
    }
    render_svg(files.directory);
    std::ifstream image(files.directory / "graph.svg", std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(image)), std::istreambuf_iterator<char>());
    image.close();
    if (content.find("<svg") == std::string::npos) throw std::runtime_error("Graphviz не создал SVG");
    fs::rename(files.directory / "graph.dot", dot);
    fs::rename(files.directory / "graph.svg", svg);
}

}
