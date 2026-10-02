#include "session-config.hpp"

namespace lt {

bool session_should_run(const SessionConfig &c, bool source_active)
{
    return c.enabled && source_active && !c.api_key.empty() &&
           !c.input_uuid.empty();
}

bool session_needs_reconnect(const SessionConfig &prev,
                             const SessionConfig &next)
{
    return prev.api_key != next.api_key || prev.model != next.model ||
           prev.target_lang != next.target_lang ||
           prev.echo_target != next.echo_target;
}

std::string api_key_hint(const std::string &key)
{
    if (key.empty()) return "";
    // Show a tail only when it is a small part of the key.
    if (key.size() < 12) return "...";
    return "..." + key.substr(key.size() - 4);
}

}
