#include "json.hpp"

#include <cstdint>
#include <regex>
#include <stdexcept>

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

}
