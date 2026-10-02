#pragma once
#include <string>
#include <vector>

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

// Several keys for one provider are stored as the provider's base variable
// plus named variants: GEMINI_API_KEY, GEMINI_API_KEY_STUDIO, ...

// True iff `name` is `base` itself or one of its `base_<LABEL>` variants.
bool env_name_belongs(const std::string &base, const std::string &name);

// Variables in the .env text that belong to `base` and have a value, in file
// order, each once.
std::vector<std::string> dotenv_names(const std::string &text,
                                      const std::string &base);

// The variable name for a user-typed key label: letters and digits are kept
// (upper-cased), anything else becomes `_`. An empty or "default" label is the
// base variable itself.
std::string env_name_with_label(const std::string &base,
                                const std::string &label);

// The readable label of a key variable: empty for the base variable, else the
// variant's suffix in lower case.
std::string env_label(const std::string &base, const std::string &name);

}
