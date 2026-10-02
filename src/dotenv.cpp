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

bool env_name_belongs(const std::string &base, const std::string &name)
{
    if (base.empty()) return false;
    if (name == base) return true;
    return name.size() > base.size() + 1 && name.compare(0, base.size(), base) == 0 &&
           name[base.size()] == '_';
}

std::vector<std::string> dotenv_names(const std::string &text,
                                      const std::string &base)
{
    std::vector<std::string> names;
    for (const auto &line : split_lines(text)) {
        std::string n, v;
        if (!parse_line(line, n, v) || !env_name_belongs(base, n)) continue;
        bool seen = false;
        for (const auto &existing : names)
            if (existing == n) seen = true;
        // Only the last definition counts, so check the effective value.
        if (!seen && !dotenv_get(text, n).empty()) names.push_back(n);
    }
    return names;
}

std::string env_name_with_label(const std::string &base,
                                const std::string &label)
{
    std::string suffix;
    for (char ch : label) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'a' && c <= 'z')
            suffix += static_cast<char>(c - 'a' + 'A');
        else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            suffix += static_cast<char>(c);
        else if (!suffix.empty() && suffix.back() != '_')
            suffix += '_';
    }
    while (!suffix.empty() && suffix.back() == '_') suffix.pop_back();
    if (suffix.empty() || suffix == "DEFAULT") return base;
    return base + "_" + suffix;
}

std::string env_label(const std::string &base, const std::string &name)
{
    if (name == base || !env_name_belongs(base, name)) return "";
    std::string label = name.substr(base.size() + 1);
    for (char &c : label)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return label;
}

}
