// Logs how each turn goes on the Hermes side, for a device without a screen:
// transcript and reply sizes, audio streams, the outcome, notices and errors.
// What was said stays out of the log.
#pragma once

#include <string_view>

namespace nabu {

// Takes every text frame from Hermes; logs the turn-related ones.
void log_turn_message(std::string_view frame);

}  // namespace nabu
