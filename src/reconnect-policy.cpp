#include "reconnect-policy.hpp"

namespace lt {

ReconnectPolicy::ReconnectPolicy(int max_attempts, uint32_t base_ms,
                                 uint32_t max_ms, uint64_t stable_ms)
    : max_attempts_(max_attempts), stable_ms_(stable_ms),
      backoff_(base_ms, max_ms)
{
}

ReconnectDecision ReconnectPolicy::on_disconnect(bool established,
                                                 uint64_t connected_ms)
{
    if (established && connected_ms >= stable_ms_) {
        // A session that ran fine dropped (e.g. the server's periodic
        // connection reset): start over and reconnect at once.
        reset();
        failed_ = 1;
        return {true, 0, failed_};
    }
    ++failed_;
    if (failed_ > max_attempts_) return {false, 0, failed_};
    return {true, backoff_.next_ms(), failed_};
}

void ReconnectPolicy::reset()
{
    failed_ = 0;
    backoff_.reset();
}

}
