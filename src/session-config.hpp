#pragma once
#include <string>

namespace lt {

// Everything one translated-audio source needs to run its translation stream.
struct SessionConfig {
    std::string api_key;
    std::string model; // model id from the catalog
    std::string target_lang;
    bool echo_target = true;
    std::string input_uuid; // UUID of the OBS source being translated
    std::string key_name;   // .env variable of the chosen key; empty = default
    bool enabled = true;    // the connection switch in the properties
};

// True iff a session should be running: it is switched on, has a key and an
// input, and its source is active in the program (hiding the source also stops
// it).
bool session_should_run(const SessionConfig &c, bool source_active);

// True iff moving from prev to next changes something the model is told at setup,
// so the connection has to be re-established. A different input only re-points
// the audio capture and keeps the connection.
bool session_needs_reconnect(const SessionConfig &prev,
                             const SessionConfig &next);

// What the UI may show of a stored key so the user can tell which one it is:
// "..." plus the last four characters. Keys too short to hide behind that show
// only "...", and an empty key gives an empty hint.
std::string api_key_hint(const std::string &key);

// Prefix for a session's log lines: "[vila-glt] 'source' key=VARIABLE: ".
// key_env is the .env variable name of the key in use, never the key itself.
// Parts that are empty are left out.
std::string log_prefix(const std::string &source_name,
                       const std::string &key_env);

}
