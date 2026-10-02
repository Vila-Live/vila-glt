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
};

// True iff a session should be running: it needs a key and an input, and its
// source has to be active in the program (hiding the source is the off switch).
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

}
