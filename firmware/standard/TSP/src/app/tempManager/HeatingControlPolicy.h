#pragma once

namespace HeatingControlPolicy {
enum class Action { Keep, Start, Stop };
constexpr Action decide(bool enabled, bool valid, float temperature, float humidity,
                         float temperatureLow, float humidityHigh) {
    if (!enabled || !valid) return Action::Stop;
    if (temperature < temperatureLow || humidity > humidityHigh) return Action::Start;
    if (temperature >= temperatureLow && humidity <= humidityHigh) return Action::Stop;
    return Action::Keep;
}
static_assert(decide(false, true, 0, 100, 15, 80) == Action::Stop,
              "Master switch always inhibits heating");
static_assert(decide(true, false, 0, 100, 15, 80) == Action::Stop,
              "Invalid SHT30 sample must stop heating");
} // namespace HeatingControlPolicy
