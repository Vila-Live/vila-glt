#pragma once
#include "ring-buffer.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace lt {

enum class ConnStatus {
    Idle,
    Connecting,
    Connected,
    Reconnecting,
    AuthError,
    QuotaError,
    Error
};

// One translation stream: a WebSocket to Gemini plus its input/output audio
// buffers. Each translated-audio source owns its own session, so several
// translations can run side by side without sharing any state.
class TranslationSession {
public:
    static constexpr int kMaxReconnectAttempts = 5;
    // A session that ran this long after setup is "stable": its first drop
    // reconnects immediately instead of counting as a failed attempt.
    static constexpr uint64_t kStableSessionMs = 30000;

    TranslationSession();
    ~TranslationSession();
    TranslationSession(const TranslationSession &) = delete;
    TranslationSession &operator=(const TranslationSession &) = delete;

    void configure(const std::string &api_key, const std::string &model,
                   const std::string &target_lang, bool echo_target);
    void stop();

    void push_input_pcm(const uint8_t *data, size_t len);
    void append_output(const uint8_t *data, size_t len);
    void signal_interrupt();
    size_t wait_and_read_output(uint8_t *out, size_t max_len, uint32_t timeout_ms);
    bool take_interrupted();
    bool take_stopped_on_error();
    uint64_t input_idle_ms();

    ConnStatus status();
    std::string status_detail();
    int reconnect_attempt();
    std::string status_text();
    bool is_running();

private:
    void run();
    void set_status(ConnStatus s, const std::string &detail = "",
                    int attempt = 0);

    ByteRingBuffer input_{16000 * 2 * 5};
    ByteRingBuffer output_{24000 * 2 * 10};
    std::condition_variable output_cv_;
    std::mutex output_wait_mtx_;
    std::atomic<bool> interrupted_{false};
    std::atomic<bool> stopped_on_error_{false};
    std::atomic<uint64_t> last_input_ms_{0};

    std::mutex cfg_mtx_;
    std::string api_key_;
    std::string model_;
    std::string target_lang_;
    bool echo_target_ = true;

    std::atomic<bool> running_{false};
    std::atomic<bool> config_changed_{false};
    std::thread thread_;

    std::mutex status_mtx_;
    ConnStatus status_ = ConnStatus::Idle;
    std::string status_detail_;
    int reconnect_attempt_ = 0;
};

}
