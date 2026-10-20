#include "translation-session.hpp"
#include "backoff.hpp"
#include "live-protocol.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <obs.h>
#include <chrono>
#include <vector>

namespace lt {

namespace {
uint64_t now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
}

TranslationSession::TranslationSession() = default;

TranslationSession::~TranslationSession() { stop(); }

void TranslationSession::set_status(ConnStatus s, const std::string &detail,
                                    int attempt)
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    status_ = s;
    status_detail_ = detail;
    reconnect_attempt_ = attempt;
}

ConnStatus TranslationSession::status()
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    return status_;
}

std::string TranslationSession::status_detail()
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    return status_detail_;
}

int TranslationSession::reconnect_attempt()
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    return reconnect_attempt_;
}

bool TranslationSession::is_running()
{
    return running_.load();
}

std::string TranslationSession::status_text()
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    switch (status_) {
    case ConnStatus::Idle:
        return "Idle";
    case ConnStatus::Connecting:
        return "Connecting...";
    case ConnStatus::Connected:
        return "Connected";
    case ConnStatus::Reconnecting:
        if (reconnect_attempt_ > 0) {
            return "Reconnecting... (" + std::to_string(reconnect_attempt_) +
                   "/" + std::to_string(kMaxReconnectAttempts) + ")";
        }
        return "Reconnecting...";
    case ConnStatus::AuthError:
        return "API key error: " + status_detail_;
    case ConnStatus::QuotaError:
        return "Quota or credit exhausted: " + status_detail_;
    case ConnStatus::Error:
        return "Connection error: " + status_detail_;
    }
    return "Unknown";
}

void TranslationSession::configure(const std::string &api_key,
                                   const std::string &model,
                                   const std::string &target_lang,
                                   bool echo_target)
{
    {
        std::lock_guard<std::mutex> lk(cfg_mtx_);
        api_key_ = api_key;
        model_ = model;
        target_lang_ = target_lang;
        echo_target_ = echo_target;
    }
    if (api_key.empty()) {
        blog(LOG_INFO, "[vila-glt] API key cleared; stopping session");
        stop();
        return;
    }
    blog(LOG_INFO,
         "[vila-glt] configuring session: model=%s target=%s echo=%s "
         "api_key=%s",
         model.c_str(), target_lang.c_str(), echo_target ? "on" : "off",
         api_key.empty() ? "empty" : "set");
    stopped_on_error_.store(false, std::memory_order_relaxed);
    config_changed_ = true;
    if (!running_.exchange(true)) {
        // A previous run() may have exited on its own (e.g. auth error) and left
        // thread_ joinable. Join it before reassigning, or the assignment would
        // std::terminate.
        if (thread_.joinable()) thread_.join();
        thread_ = std::thread([this] { run(); });
    }
}

void TranslationSession::stop()
{
    running_.exchange(false);
    // Always join: run() may have already cleared running_ itself (auth error),
    // in which case the thread is still joinable and must be reaped here so the
    // session's destruction does not std::terminate.
    if (thread_.joinable()) thread_.join();
    input_.clear();
    output_.clear();
    stopped_on_error_.store(false, std::memory_order_relaxed);
    set_status(ConnStatus::Idle);
}

void TranslationSession::push_input_pcm(const uint8_t *data, size_t len)
{
    last_input_ms_.store(now_ms(), std::memory_order_relaxed);
    input_.write(data, len);
}

void TranslationSession::append_output(const uint8_t *data, size_t len)
{
    output_.write(data, len);
    // Serialize with a consumer that is between checking the predicate and
    // entering wait_for, so the notify cannot be lost.
    { std::lock_guard<std::mutex> lk(output_wait_mtx_); }
    output_cv_.notify_one();
}

void TranslationSession::signal_interrupt()
{
    output_.clear();
    interrupted_.store(true, std::memory_order_relaxed);
    { std::lock_guard<std::mutex> lk(output_wait_mtx_); }
    output_cv_.notify_one();
}

size_t TranslationSession::wait_and_read_output(uint8_t *out, size_t max_len,
                                                uint32_t timeout_ms)
{
    {
        std::unique_lock<std::mutex> lk(output_wait_mtx_);
        output_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this] { return output_.size() > 0; });
    }
    return output_.read(out, max_len);
}

bool TranslationSession::take_interrupted()
{
    return interrupted_.exchange(false, std::memory_order_relaxed);
}

bool TranslationSession::take_stopped_on_error()
{
    return stopped_on_error_.exchange(false, std::memory_order_relaxed);
}

uint64_t TranslationSession::input_idle_ms()
{
    uint64_t last = last_input_ms_.load(std::memory_order_relaxed);
    if (last == 0) return UINT64_MAX;
    uint64_t now = now_ms();
    return now >= last ? now - last : 0;
}

void TranslationSession::run()
{
    Backoff backoff(1000, 30000);
    int failed_attempts = 0;

    while (running_) {
        std::string key, model, lang;
        bool echo;
        {
            std::lock_guard<std::mutex> lk(cfg_mtx_);
            key = api_key_;
            model = model_;
            lang = target_lang_;
            echo = echo_target_;
        }
        if (config_changed_.exchange(false)) {
            failed_attempts = 0;
            backoff.reset();
        }

        ix::WebSocket ws;
        ws.disableAutomaticReconnection();
        std::string url =
            "wss://generativelanguage.googleapis.com/ws/"
            "google.ai.generativelanguage.v1beta.GenerativeService."
            "BidiGenerateContent?key=" + key;
        ws.setUrl(url);

        std::atomic<bool> setup_ready{false};
        std::atomic<bool> session_established{false};
        std::atomic<bool> closed{false};
        std::mutex disconnect_mtx;
        DisconnectInfo disconnect_info{DisconnectReason::Transient,
                                       "Connection lost"};
        std::atomic<bool> has_disconnect_info{false};

        ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
            if (msg->type == ix::WebSocketMessageType::Open) {
                std::string setup = build_setup_message(model, lang, echo);
                ws.send(setup);
                blog(LOG_INFO, "[vila-glt] websocket opened; setup sent");
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                ServerMessage m = parse_server_message(msg->str);
                if (m.kind == ServerMessage::Kind::SetupComplete) {
                    session_established = true;
                    setup_ready = true;
                    set_status(ConnStatus::Connected);
                    blog(LOG_INFO,
                         "[vila-glt] setup complete; session connected");
                }
                if (m.interrupted) {
                    blog(LOG_INFO,
                         "[vila-glt] turn interrupted; dropping pending audio");
                    signal_interrupt();
                }
                if (m.kind == ServerMessage::Kind::Audio && !m.audio.empty()) {
                    if (!setup_ready) {
                        session_established = true;
                        setup_ready = true;
                        set_status(ConnStatus::Connected);
                    }
                    blog(LOG_DEBUG, "[vila-glt] received audio bytes: %zu",
                         m.audio.size());
                    append_output(m.audio.data(), m.audio.size());
                } else if (m.kind == ServerMessage::Kind::Error) {
                    DisconnectInfo info = classify_disconnect(
                        m.error_code, m.error_message, m.error_status);
                    if (info.reason == DisconnectReason::Transient &&
                        m.error_code != 500 && m.error_code != 503 &&
                        m.error_status != "INTERNAL" &&
                        m.error_status != "UNAVAILABLE") {
                        info.reason = DisconnectReason::ConfigError;
                    }
                    blog(LOG_ERROR,
                         "[vila-glt] server error (code=%d, status=%s): %s",
                         m.error_code, m.error_status.c_str(),
                         m.error_message.c_str());
                    {
                        std::lock_guard<std::mutex> lk(disconnect_mtx);
                        disconnect_info = info;
                        has_disconnect_info = true;
                    }
                    setup_ready = false;
                    closed = true;
                }
            } else if (msg->type == ix::WebSocketMessageType::Close ||
                       msg->type == ix::WebSocketMessageType::Error) {
                DisconnectInfo info;
                if (msg->type == ix::WebSocketMessageType::Error) {
                    info = classify_disconnect(msg->errorInfo.http_status,
                                               msg->errorInfo.reason);
                    blog(LOG_ERROR,
                         "[vila-glt] websocket error (http=%d): %s",
                         msg->errorInfo.http_status,
                         msg->errorInfo.reason.c_str());
                } else {
                    info = classify_disconnect(msg->closeInfo.code,
                                               msg->closeInfo.reason);
                    blog(LOG_WARNING,
                         "[vila-glt] websocket closed (code=%u): %s",
                         static_cast<unsigned>(msg->closeInfo.code),
                         msg->closeInfo.reason.c_str());
                }
                {
                    std::lock_guard<std::mutex> lk(disconnect_mtx);
                    if (!has_disconnect_info) {
                        disconnect_info = info;
                        has_disconnect_info = true;
                    }
                }
                setup_ready = false;
                closed = true;
            }
        });

        if (failed_attempts == 0) {
            set_status(ConnStatus::Connecting);
        }
        uint64_t attempt_start_ms = now_ms();
        ws.start();

        std::vector<uint8_t> chunk(3200);
        while (running_ && !config_changed_ && !closed) {
            if (!setup_ready) {
                if (now_ms() - attempt_start_ms > 15000) {
                    blog(LOG_WARNING,
                         "[vila-glt] connection setup timed out after 15s");
                    std::lock_guard<std::mutex> lk(disconnect_mtx);
                    if (!has_disconnect_info) {
                        disconnect_info = {DisconnectReason::Transient,
                                           "Connection setup timed out"};
                        has_disconnect_info = true;
                    }
                    closed = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            size_t n = input_.read(chunk.data(), chunk.size());
            if (n == chunk.size()) {
                std::string m =
                    build_realtime_input_message(chunk.data(), chunk.size());
                ws.send(m);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ws.stop();

        if (config_changed_) continue;
        if (!running_) break;

        bool was_connected = session_established.load();
        if (was_connected) {
            failed_attempts = 0;
            backoff.reset();
        }

        DisconnectInfo info;
        {
            std::lock_guard<std::mutex> lk(disconnect_mtx);
            info = disconnect_info;
        }

        if (!info.should_reconnect()) {
            ConnStatus err_status = ConnStatus::Error;
            if (info.reason == DisconnectReason::AuthError)
                err_status = ConnStatus::AuthError;
            else if (info.reason == DisconnectReason::QuotaExceeded)
                err_status = ConnStatus::QuotaError;

            blog(LOG_ERROR,
                 "[vila-glt] fatal error, stopping session: %s",
                 info.detail.c_str());
            set_status(err_status, info.detail);
            stopped_on_error_.store(true, std::memory_order_relaxed);
            running_ = false;
            break;
        }

        ++failed_attempts;
        if (failed_attempts > kMaxReconnectAttempts) {
            blog(LOG_ERROR,
                 "[vila-glt] reconnect failed after %d attempts; giving "
                 "up (last error: %s)",
                 kMaxReconnectAttempts, info.detail.c_str());
            set_status(ConnStatus::Error, info.detail);
            stopped_on_error_.store(true, std::memory_order_relaxed);
            running_ = false;
            break;
        }

        uint32_t wait =
            (was_connected && failed_attempts == 1) ? 0 : backoff.next_ms();

        blog(LOG_WARNING,
             "[vila-glt] reconnecting (attempt %d/%d, wait %u ms): %s",
             failed_attempts, kMaxReconnectAttempts, wait, info.detail.c_str());
        set_status(ConnStatus::Reconnecting, info.detail, failed_attempts);

        for (uint32_t waited = 0;
             waited < wait && running_ && !config_changed_; waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (wait > 0) {
            input_.clear();
        }
    }

    ConnStatus final_status = status();
    if (final_status != ConnStatus::AuthError &&
        final_status != ConnStatus::QuotaError &&
        final_status != ConnStatus::Error) {
        set_status(ConnStatus::Idle);
    }
}

}
