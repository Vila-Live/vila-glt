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
    Transient,
    AuthError,
    QuotaExceeded,
    ConfigError,
};

struct DisconnectInfo {
    DisconnectReason reason = DisconnectReason::Transient;
    std::string detail;

    bool should_reconnect() const
    {
        return reason == DisconnectReason::Transient;
    }
};

DisconnectInfo classify_disconnect(int code, const std::string &reason,
                                   const std::string &status = "");

}
