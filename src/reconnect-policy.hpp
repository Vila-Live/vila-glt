#pragma once
#include "backoff.hpp"
#include <cstdint>

namespace lt {

// What to do after a connection attempt ended on a retryable error.
struct ReconnectDecision {
    bool retry = false;   // false: give up, the attempts are used up
    uint32_t wait_ms = 0; // how long to wait before the next attempt
    int attempt = 0;      // 1-based number of the attempt about to start
};

// Decides when a session reconnects after a retryable disconnect. The first
// drop of a stable session (one that ran for at least stable_ms after its setup
// completed) reconnects immediately; any other failure — an attempt that never
// completed setup, or a session that dropped soon after it — counts against
// max_attempts and waits with exponential backoff.
class ReconnectPolicy {
public:
    ReconnectPolicy(int max_attempts, uint32_t base_ms, uint32_t max_ms,
                    uint64_t stable_ms);

    // established: the setup completed; connected_ms: how long the session
    // ran after that (ignored when not established).
    ReconnectDecision on_disconnect(bool established, uint64_t connected_ms);

    // Forget earlier failures, e.g. after the settings changed.
    void reset();

    int failed_attempts() const { return failed_; }

private:
    int max_attempts_;
    uint64_t stable_ms_;
    Backoff backoff_;
    int failed_ = 0;
};

}
