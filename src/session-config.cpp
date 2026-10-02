#include "session-config.hpp"

namespace lt {

bool session_should_run(const SessionConfig &c)
{
    return !c.api_key.empty() && !c.input_uuid.empty();
}

bool session_needs_reconnect(const SessionConfig &prev,
                             const SessionConfig &next)
{
    return prev.api_key != next.api_key ||
           prev.target_lang != next.target_lang ||
           prev.echo_target != next.echo_target;
}

}
