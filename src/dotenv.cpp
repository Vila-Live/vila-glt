#include "dotenv.hpp"
#include <vector>

namespace lt {

namespace {

std::string trim(const std::string &s)
{
    const char *ws = " \t\r";
    size_t first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// Parses one line into name/value. False for blanks, comments and anything
// without a `=`.
bool parse_line(const std::string &raw, std::string &name, std::string &value)
{
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return false;
    if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));

    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    name = trim(line.substr(0, eq));
    value = trim(line.substr(eq + 1));

    bool quoted = value.size() >= 2 && value.front() == value.back() &&
                  (value.front() == '"' || value.front() == '\'');
    if (quoted) {
        value = value.substr(1, value.size() - 2);
    } else {
        // An unquoted value ends at an inline comment.
        size_t comment = value.find(" #");
        if (comment != std::string::npos) value = trim(value.substr(0, comment));
    }
    return !name.empty();
}

}

std::string dotenv_get(const std::string &text, const std::string &name)
{
    std::string found;
    for (const auto &line : split_lines(text)) {
        std::string n, v;
        if (parse_line(line, n, v) && n == name) found = v;
    }
    return found;
}

std::string dotenv_set(const std::string &text, const std::string &name,
                       const std::string &value)
{
    std::string out;
    for (const auto &line : split_lines(text)) {
        std::string n, v;
        if (parse_line(line, n, v) && n == name) continue;
        out += line;
        out += '\n';
    }
    if (!value.empty()) out += name + "=" + value + "\n";
    return out;
}

}
