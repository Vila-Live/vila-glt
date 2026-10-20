#include <catch2/catch_test_macros.hpp>
#include "reconnect-policy.hpp"

using lt::ReconnectDecision;
using lt::ReconnectPolicy;

namespace {

// 5 attempts, backoff 1 s .. 30 s, a session counts as stable after 30 s.
ReconnectPolicy make_policy()
{
    return ReconnectPolicy(5, 1000, 30000, 30000);
}

}

TEST_CASE("first drop of a stable session reconnects immediately")
{
    ReconnectPolicy p = make_policy();
    ReconnectDecision d = p.on_disconnect(true, 10 * 60 * 1000);
    REQUIRE(d.retry);
    REQUIRE(d.wait_ms == 0);
    REQUIRE(d.attempt == 1);
}

TEST_CASE("attempts that never complete setup back off, then give up")
{
    ReconnectPolicy p = make_policy();
    const uint32_t waits[] = {1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < 5; ++i) {
        ReconnectDecision d = p.on_disconnect(false, 0);
        REQUIRE(d.retry);
        REQUIRE(d.wait_ms == waits[i]);
        REQUIRE(d.attempt == i + 1);
    }
    REQUIRE_FALSE(p.on_disconnect(false, 0).retry);
}

TEST_CASE("sessions that drop soon after setup count as failed attempts")
{
    // A server that accepts the setup and closes a few seconds later must not
    // be hammered with immediate reconnects forever.
    ReconnectPolicy p = make_policy();
    REQUIRE(p.on_disconnect(true, 60000).wait_ms == 0);
    const uint32_t waits[] = {1000, 2000, 4000, 8000};
    for (int i = 0; i < 4; ++i) {
        ReconnectDecision d = p.on_disconnect(true, 2000);
        REQUIRE(d.retry);
        REQUIRE(d.wait_ms == waits[i]);
        REQUIRE(d.attempt == i + 2);
    }
    REQUIRE_FALSE(p.on_disconnect(true, 2000).retry);
}

TEST_CASE("a stable session clears earlier failures")
{
    ReconnectPolicy p = make_policy();
    p.on_disconnect(false, 0);
    p.on_disconnect(false, 0);
    ReconnectDecision d = p.on_disconnect(true, 30000);
    REQUIRE(d.retry);
    REQUIRE(d.wait_ms == 0);
    REQUIRE(d.attempt == 1);
    // ...and the next quick failure starts the backoff from the beginning.
    REQUIRE(p.on_disconnect(false, 0).wait_ms == 1000);
}

TEST_CASE("reset starts over after a settings change")
{
    ReconnectPolicy p = make_policy();
    p.on_disconnect(false, 0);
    p.on_disconnect(false, 0);
    p.reset();
    REQUIRE(p.failed_attempts() == 0);
    ReconnectDecision d = p.on_disconnect(false, 0);
    REQUIRE(d.wait_ms == 1000);
    REQUIRE(d.attempt == 1);
}
