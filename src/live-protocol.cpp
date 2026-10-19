#include "live-protocol.hpp"
#include "base64.hpp"
#include <initializer_list>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace lt {

namespace {

std::string to_lower_ascii(const std::string &s)
{
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool contains_any(const std::string &haystack,
                  std::initializer_list<const char *> needles)
{
    for (const char *n : needles) {
        if (haystack.find(n) != std::string::npos) return true;
    }
    return false;
}

std::string default_disconnect_detail(int code, const std::string &reason)
{
    if (!reason.empty()) return reason;
    if (code > 0) return "Connection closed (code " + std::to_string(code) + ")";
    return "Connection lost";
}

} // namespace

std::string build_setup_message(const std::string &model_id,
                                const std::string &target, bool echo)
{
    json j;
    j["setup"]["model"] = "models/" + model_id;
    j["setup"]["generationConfig"]["responseModalities"] = json::array({"AUDIO"});
    j["setup"]["generationConfig"]["translationConfig"]["targetLanguageCode"] = target;
    j["setup"]["generationConfig"]["translationConfig"]["echoTargetLanguage"] = echo;
    return j.dump();
}

std::string build_realtime_input_message(const uint8_t *pcm, size_t len)
{
    json chunk;
    chunk["mimeType"] = "audio/pcm;rate=16000";
    chunk["data"] = base64_encode(pcm, len);
    json j;
    j["realtimeInput"]["audio"] = chunk;
    return j.dump();
}

ServerMessage parse_server_message(const std::string &text)
{
    ServerMessage m;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        m.kind = ServerMessage::Kind::Other;
        return m;
    }

    if (j.contains("error") && j["error"].is_object()) {
        m.kind = ServerMessage::Kind::Error;
        const auto &err = j["error"];
        if (err.contains("code") && err["code"].is_number_integer())
            m.error_code = err["code"].get<int>();
        if (err.contains("status") && err["status"].is_string())
            m.error_status = err["status"].get<std::string>();
        if (err.contains("message") && err["message"].is_string())
            m.error_message = err["message"].get<std::string>();
        return m;
    }

    if (j.contains("setupComplete")) {
        m.kind = ServerMessage::Kind::SetupComplete;
        return m;
    }

    if (j.contains("serverContent")) {
        const auto &sc = j["serverContent"];
        m.turn_complete = sc.value("turnComplete", false);
        m.generation_complete = sc.value("generationComplete", false);
        m.interrupted = sc.value("interrupted", false);
        if (sc.contains("modelTurn") && sc["modelTurn"].contains("parts")) {
            for (const auto &part : sc["modelTurn"]["parts"]) {
                if (part.contains("inlineData") &&
                    part["inlineData"].contains("data")) {
                    auto mime = part["inlineData"].value("mimeType", "");
                    if (mime.rfind("audio/pcm", 0) == 0) {
                        m.kind = ServerMessage::Kind::Audio;
                        auto audio = base64_decode(
                            part["inlineData"]["data"].get<std::string>());
                        m.audio.insert(m.audio.end(), audio.begin(), audio.end());
                    }
                }
            }
        }
        if (m.kind == ServerMessage::Kind::Audio) return m;
        m.kind = ServerMessage::Kind::Other;
        return m;
    }

    m.kind = ServerMessage::Kind::Other;
    return m;
}

DisconnectInfo classify_disconnect(int code, const std::string &reason,
                                   const std::string &status)
{
    std::string combined = to_lower_ascii(reason + " " + status);
    std::string detail = default_disconnect_detail(code, reason);

    if (code == 429 || code == 8 ||
        contains_any(combined, {"resource_exhausted", "resource exhausted",
                                "resource has been exhausted", "quota",
                                "billing", "insufficient credit",
                                "credit balance", "rate limit", "rate_limit",
                                "too many requests"})) {
        return {DisconnectReason::QuotaExceeded, detail};
    }

    if (code == 401 || code == 403 || code == 7 || code == 16 ||
        contains_any(combined, {"api key", "api_key", "apikey",
                                "unauthenticated", "unauthorized",
                                "permission_denied", "permission denied",
                                "forbidden", "invalid key",
                                "invalid credentials"})) {
        return {DisconnectReason::AuthError, detail};
    }

    if (code == 400 || code == 404 || code == 1002 || code == 1003 ||
        code == 1007 || code == 3 || code == 5 ||
        contains_any(combined, {"invalid_argument", "invalid argument",
                                "invalid value", "not_found", "not found",
                                "not supported", "unsupported",
                                "unimplemented"})) {
        return {DisconnectReason::ConfigError, detail};
    }

    return {DisconnectReason::Transient, detail};
}

}
