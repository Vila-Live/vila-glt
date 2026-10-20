#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lt {

std::string build_setup_message(const std::string &model_id,
                                const std::string &target_language_code,
                                bool echo_target_language);

std::string build_realtime_input_message(const uint8_t *pcm, size_t len);

struct ServerMessage {
    enum class Kind { Audio, SetupComplete, Error, Other };
    Kind kind = Kind::Other;
    std::vector<uint8_t> audio;
    int error_code = 0;
    std::string error_status;
    std::string error_message;
    bool turn_complete = false;
    bool generation_complete = false;
    bool interrupted = false;
};

ServerMessage parse_server_message(const std::string &json_text);

enum class DisconnectReason {
    Transient,   // network drop, server reset or 5xx: reconnect
    RateLimited, // 429 / quota: reconnect with backoff (bounded)
    AuthError,   // bad or unauthorised API key: stop
    OutOfCredit, // 402 / prepaid credits or billing: stop
    ConfigError, // other 4xx, unknown model, unsupported setup: stop
};

struct DisconnectInfo {
    DisconnectReason reason = DisconnectReason::Transient;
    std::string detail;

    bool should_reconnect() const
    {
        return reason == DisconnectReason::Transient ||
               reason == DisconnectReason::RateLimited;
    }
};

// Sorts a disconnect by what to do about it. code is a WebSocket close code or
// an HTTP status (Google's docs list the HTTP ones); reason/status are the
// server's text, which is often the only hint since the Live API's close codes
// are not documented. Follows the docs: retry 408, 429 and 5xx, do not retry
// other 4xx.
DisconnectInfo classify_disconnect(int code, const std::string &reason,
                                   const std::string &status = "");

}
