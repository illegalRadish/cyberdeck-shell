#include "core/JsonLine.hpp"

#include <cctype>
#include <cstdlib>

namespace cyberdeck::jsonline {

namespace {

void skipSpace(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool readHex4(const std::string& s, std::size_t& i, unsigned int& out) {
    if (i + 4 > s.size()) {
        return false;
    }
    unsigned int value = 0;
    for (int n = 0; n < 4; ++n) {
        const char c = s[i + n];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<unsigned int>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<unsigned int>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<unsigned int>(c - 'A' + 10);
        } else {
            return false;
        }
    }
    i += 4;
    out = value;
    return true;
}

// Reads a JSON string starting at the opening quote. Leaves `i` past the close.
bool parseString(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') {
            ++i;
            return true;
        }
        if (c != '\\') {
            out.push_back(c);
            ++i;
            continue;
        }
        ++i;
        if (i >= s.size()) {
            return false;
        }
        const char esc = s[i++];
        switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned int cp = 0;
                if (!readHex4(s, i, cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate
                    unsigned int low = 0;
                    if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        const std::size_t save = i;
                        i += 2;
                        if (readHex4(s, i, low) && low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            i = save;
                            cp = 0xFFFD;
                        }
                    } else {
                        cp = 0xFFFD;
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {  // lone low surrogate
                    cp = 0xFFFD;
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return false;
        }
    }
    return false;  // unterminated
}

// Advances past a value we don't care about, tracking nesting so that braces
// or brackets inside strings never confuse the depth count.
bool skipValue(const std::string& s, std::size_t& i) {
    skipSpace(s, i);
    if (i >= s.size()) {
        return false;
    }
    if (s[i] == '"') {
        std::string scratch;
        return parseString(s, i, scratch);
    }
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') {
                std::string scratch;
                if (!parseString(s, i, scratch)) {
                    return false;
                }
                continue;
            }
            if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
                if (depth == 0) {
                    ++i;
                    return true;
                }
            }
            ++i;
        }
        return false;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}') {  // number/true/false/null
        ++i;
    }
    return true;
}

}  // namespace

bool isObject(const std::string& line) {
    std::size_t i = 0;
    skipSpace(line, i);
    return i < line.size() && line[i] == '{';
}

std::optional<std::string> field(const std::string& line, std::string_view key) {
    std::size_t i = 0;
    skipSpace(line, i);
    if (i >= line.size() || line[i] != '{') {
        return std::nullopt;
    }
    ++i;

    while (true) {
        skipSpace(line, i);
        if (i >= line.size()) {
            return std::nullopt;
        }
        if (line[i] == '}') {
            return std::nullopt;  // ran out of keys
        }

        std::string name;
        if (!parseString(line, i, name)) {
            return std::nullopt;
        }
        skipSpace(line, i);
        if (i >= line.size() || line[i] != ':') {
            return std::nullopt;
        }
        ++i;
        skipSpace(line, i);

        if (name == key) {
            if (i < line.size() && line[i] == '"') {
                std::string value;
                if (parseString(line, i, value)) {
                    return value;
                }
            }
            return std::nullopt;  // present but not a string: not our contract
        }

        if (!skipValue(line, i)) {
            return std::nullopt;
        }
        skipSpace(line, i);
        if (i < line.size() && line[i] == ',') {
            ++i;
            continue;
        }
        if (i < line.size() && line[i] == '}') {
            return std::nullopt;
        }
        return std::nullopt;
    }
}

int toInt(const std::optional<std::string>& value, int fallback) {
    if (!value || value->empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value->c_str(), &end, 10);
    if (end == value->c_str()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

}  // namespace cyberdeck::jsonline
