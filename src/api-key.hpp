#pragma once
#include <cstdint>
#include <string>

namespace lt {

// The one Gemini API key shared by every translated-audio source in every
// scene collection. It lives as GEMINI_API_KEY in a `.env` file in the OBS
// config folder (next to global.ini) instead of the source settings, so it is
// not written to the scene collection and cannot be read back through OBS
// WebSocket. The file is plain text and the same on every platform; other
// variables in it are left untouched.

// Loads the stored key; call once at module load.
void api_key_load();

std::string api_key_get();

// Stores a new key (an empty key forgets it). No-op if unchanged.
void api_key_set(const std::string &key);

// Bumped whenever the key changes, so sources can notice without polling the
// key itself.
uint32_t api_key_version();

}
