#include <catch2/catch_test_macros.hpp>
#include "session-config.hpp"

using lt::SessionConfig;

namespace {

SessionConfig valid()
{
    SessionConfig c;
    c.api_key = "key";
    c.target_lang = "en";
    c.echo_target = true;
    c.input_uuid = "uuid-a";
    return c;
}

}

TEST_CASE("session runs only with both an API key and an input source")
{
    REQUIRE(lt::session_should_run(valid(), true));

    SessionConfig no_key = valid();
    no_key.api_key.clear();
    REQUIRE_FALSE(lt::session_should_run(no_key, true));

    SessionConfig no_input = valid();
    no_input.input_uuid.clear();
    REQUIRE_FALSE(lt::session_should_run(no_input, true));
}

TEST_CASE("session does not run while its source is not active in the program")
{
    // Hiding the source (or leaving its scene) is the off switch: nothing is
    // streamed to Gemini until it is shown again.
    REQUIRE_FALSE(lt::session_should_run(valid(), false));
}

TEST_CASE("changing what Gemini sees at setup requires a reconnect")
{
    SessionConfig key = valid();
    key.api_key = "other";
    REQUIRE(lt::session_needs_reconnect(valid(), key));

    SessionConfig lang = valid();
    lang.target_lang = "ja";
    REQUIRE(lt::session_needs_reconnect(valid(), lang));

    SessionConfig echo = valid();
    echo.echo_target = false;
    REQUIRE(lt::session_needs_reconnect(valid(), echo));
}

TEST_CASE("an unchanged config or a different input does not reconnect")
{
    REQUIRE_FALSE(lt::session_needs_reconnect(valid(), valid()));

    // Switching the input source only re-points the capture callback; the
    // open connection keeps streaming.
    SessionConfig input = valid();
    input.input_uuid = "uuid-b";
    REQUIRE_FALSE(lt::session_needs_reconnect(valid(), input));
}
