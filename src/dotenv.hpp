#pragma once
#include <string>

namespace lt {

// Minimal .env (KEY=VALUE per line) reading and editing, the same on every
// platform. Understands blank lines, `#` comments, an optional `export `
// prefix, spaces around `=`, single/double quoted values and CRLF line ends.

// Value of `name` in the .env text (the last definition wins), or empty.
std::string dotenv_get(const std::string &text, const std::string &name);

// The .env text with `name` set to `value`; all other lines are kept as they
// are. An empty value removes the variable.
std::string dotenv_set(const std::string &text, const std::string &name,
                       const std::string &value);

}
