#pragma once
#include <cstdint>
#include <string>

namespace lt {

// API keys shared by every translated-audio source in every scene collection.
// They live in a `.env` file in the OBS config folder (next to global.ini), one
// variable per provider (e.g. GEMINI_API_KEY; the model catalog names the
// variable), instead of the source settings — so they are not written to the
// scene collection and cannot be read back through OBS WebSocket. The file is
// plain text and the same on every platform; other variables in it are left
// untouched.

// Reads the .env file; call once at module load.
void api_key_load();

std::string api_key_get(const std::string &env_name);

// Stores a new key (an empty key forgets it). No-op if unchanged.
void api_key_set(const std::string &env_name, const std::string &key);

// Bumped whenever any key changes, so sources can notice without polling the
// keys themselves.
uint32_t api_key_version();

}
