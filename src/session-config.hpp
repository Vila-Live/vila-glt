#pragma once
#include <string>

namespace lt {

// Everything one translated-audio source needs to run its translation stream.
struct SessionConfig {
    std::string api_key;
    std::string target_lang;
    bool echo_target = true;
    std::string input_uuid; // UUID of the OBS source being translated
};

// True iff a session should be running: it needs both a key and an input.
bool session_should_run(const SessionConfig &c);

// True iff moving from prev to next changes something Gemini is told at setup,
// so the connection has to be re-established. A different input only re-points
// the audio capture and keeps the connection.
bool session_needs_reconnect(const SessionConfig &prev,
                             const SessionConfig &next);

}
