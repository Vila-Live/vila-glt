#include <catch2/catch_test_macros.hpp>
#include "live-protocol.hpp"
#include <nlohmann/json.hpp>

using namespace lt;
using nlohmann::json;

TEST_CASE("setup message has model and translation config")
{
    std::string msg = build_setup_message("gemini-3.5-live-translate-preview",
                                          "zh-TW", true);
    json j = json::parse(msg);
    REQUIRE(j["setup"]["model"] == "models/gemini-3.5-live-translate-preview");
    REQUIRE(j["setup"]["generationConfig"]["responseModalities"][0] == "AUDIO");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["targetLanguageCode"] == "zh-TW");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["echoTargetLanguage"] == true);
}

TEST_CASE("setup message can disable target-language echo")
{
    std::string msg = build_setup_message("some-other-model", "en", false);
    json j = json::parse(msg);
    REQUIRE(j["setup"]["model"] == "models/some-other-model");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["targetLanguageCode"] == "en");
    REQUIRE(j["setup"]["generationConfig"]["translationConfig"]["echoTargetLanguage"] == false);
}

TEST_CASE("realtime input message carries base64 pcm with correct mime")
{
    std::vector<uint8_t> pcm{0x01, 0x02, 0x03, 0x04};
    std::string msg = build_realtime_input_message(pcm.data(), pcm.size());
    json j = json::parse(msg);
    auto chunk = j["realtimeInput"]["audio"];
    REQUIRE(chunk["mimeType"] == "audio/pcm;rate=16000");
    REQUIRE(chunk["data"].get<std::string>() == "AQIDBA==");
}

TEST_CASE("parse extracts pcm audio from serverContent")
{
    std::string server = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(server);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
}

TEST_CASE("parse concatenates multiple pcm audio parts from one server message")
{
    std::string server = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQI=" } },
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AwQ=" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(server);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
}

TEST_CASE("parse flags setup-rejected / error messages")
{
    std::string err = R"({"error": {"code": 400, "message": "API key not valid"}})";
    ServerMessage m = parse_server_message(err);
    REQUIRE(m.kind == ServerMessage::Kind::Error);
    REQUIRE(m.error_message.find("API key") != std::string::npos);
}

TEST_CASE("parse ignores unrelated messages gracefully")
{
    ServerMessage m = parse_server_message(R"({"setupComplete": {}})");
    REQUIRE(m.kind == ServerMessage::Kind::SetupComplete);
}

TEST_CASE("parse flags turnComplete and generationComplete on serverContent")
{
    std::string s =
        R"({"serverContent": {"generationComplete": true, "turnComplete": true}})";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.turn_complete == true);
    REQUIRE(m.generation_complete == true);
    REQUIRE(m.interrupted == false);
}

TEST_CASE("parse flags interrupted on serverContent")
{
    ServerMessage m = parse_server_message(R"({"serverContent": {"interrupted": true}})");
    REQUIRE(m.interrupted == true);
    REQUIRE(m.kind == ServerMessage::Kind::Other);
}

TEST_CASE("parse keeps audio and turnComplete in one message")
{
    std::string s = R"({
      "serverContent": { "turnComplete": true, "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
    REQUIRE(m.turn_complete == true);
}

TEST_CASE("parse leaves turn flags false when absent")
{
    std::string s = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.turn_complete == false);
    REQUIRE(m.generation_complete == false);
    REQUIRE(m.interrupted == false);
}

TEST_CASE("parse extracts error code and status from server error")
{
    std::string err = R"({
      "error": {
        "code": 429,
        "status": "RESOURCE_EXHAUSTED",
        "message": "Resource has been exhausted (e.g. check quota)."
      }
    })";
    ServerMessage m = parse_server_message(err);
    REQUIRE(m.kind == ServerMessage::Kind::Error);
    REQUIRE(m.error_code == 429);
    REQUIRE(m.error_status == "RESOURCE_EXHAUSTED");
    REQUIRE(m.error_message == "Resource has been exhausted (e.g. check quota).");
}

TEST_CASE("classify_disconnect detects quota and credit exhaustion as non-retryable")
{
    auto q1 = classify_disconnect(
        429, "Resource has been exhausted (e.g. check quota).",
        "RESOURCE_EXHAUSTED");
    REQUIRE(q1.reason == DisconnectReason::QuotaExceeded);
    REQUIRE_FALSE(q1.should_reconnect());

    // Gemini Live often closes WS with 1008 or 1011 when quota/credits run out
    auto q2 = classify_disconnect(
        1008, "Quota exceeded for quota metric 'GenerateContent' and limit");
    REQUIRE(q2.reason == DisconnectReason::QuotaExceeded);
    REQUIRE_FALSE(q2.should_reconnect());

    auto q3 = classify_disconnect(
        1011, "Resource has been exhausted (e.g. check quota).");
    REQUIRE(q3.reason == DisconnectReason::QuotaExceeded);
    REQUIRE_FALSE(q3.should_reconnect());

    auto q4 = classify_disconnect(
        1008, "Billing account has insufficient credit balance.");
    REQUIRE(q4.reason == DisconnectReason::QuotaExceeded);
    REQUIRE_FALSE(q4.should_reconnect());
}

TEST_CASE("classify_disconnect detects API key and auth errors as non-retryable")
{
    // Google returns 400 or WS 1008 when the API key is invalid
    auto a1 = classify_disconnect(
        400, "API key not valid. Please pass a valid API key.",
        "INVALID_ARGUMENT");
    REQUIRE(a1.reason == DisconnectReason::AuthError);
    REQUIRE_FALSE(a1.should_reconnect());

    auto a2 = classify_disconnect(
        1008, "API key not valid. Please pass a valid API key.");
    REQUIRE(a2.reason == DisconnectReason::AuthError);
    REQUIRE_FALSE(a2.should_reconnect());

    auto a3 = classify_disconnect(403, "The caller does not have permission",
                                  "PERMISSION_DENIED");
    REQUIRE(a3.reason == DisconnectReason::AuthError);
    REQUIRE_FALSE(a3.should_reconnect());
}

TEST_CASE("classify_disconnect detects invalid model or argument as non-retryable")
{
    auto c1 = classify_disconnect(
        1007, "models/gemini-old is not found for API version v1beta");
    REQUIRE(c1.reason == DisconnectReason::ConfigError);
    REQUIRE_FALSE(c1.should_reconnect());

    auto c2 = classify_disconnect(400, "Invalid value at 'translation_config'",
                                  "INVALID_ARGUMENT");
    REQUIRE(c2.reason == DisconnectReason::ConfigError);
    REQUIRE_FALSE(c2.should_reconnect());
}

TEST_CASE("classify_disconnect treats network drops and server resets as retryable")
{
    auto t1 = classify_disconnect(1006, "Abnormal closure");
    REQUIRE(t1.reason == DisconnectReason::Transient);
    REQUIRE(t1.should_reconnect());

    auto t2 = classify_disconnect(1011, "Internal error encountered.");
    REQUIRE(t2.reason == DisconnectReason::Transient);
    REQUIRE(t2.should_reconnect());

    auto t3 = classify_disconnect(1000, "");
    REQUIRE(t3.reason == DisconnectReason::Transient);
    REQUIRE(t3.should_reconnect());

    auto t4 = classify_disconnect(1008, "Session deadline exceeded");
    REQUIRE(t4.reason == DisconnectReason::Transient);
    REQUIRE(t4.should_reconnect());

    auto t5 = classify_disconnect(503, "Service Unavailable");
    REQUIRE(t5.reason == DisconnectReason::Transient);
    REQUIRE(t5.should_reconnect());

    auto t6 = classify_disconnect(0, "Connection reset by peer");
    REQUIRE(t6.reason == DisconnectReason::Transient);
    REQUIRE(t6.should_reconnect());
}

