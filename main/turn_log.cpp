#include "turn_log.hpp"

#include "esp_log.h"
#include "hg/json.hpp"

namespace nabu {
namespace {

const char* TAG = "nabu.turn";

// Gadget protocol message types.
constexpr std::string_view kTurnStart = "turn.start";
constexpr std::string_view kTranscript = "transcript";
constexpr std::string_view kReply = "reply";
constexpr std::string_view kAudioStart = "audio.start";
constexpr std::string_view kAudioEnd = "audio.end";
constexpr std::string_view kAudioAbort = "audio.abort";
constexpr std::string_view kTurnEnd = "turn.end";
constexpr std::string_view kNotice = "notice";
constexpr std::string_view kError = "error";

// Gadget protocol message fields.
constexpr std::string_view kTypeField = "type";
constexpr std::string_view kTextField = "text";
constexpr std::string_view kInterimField = "interim";
constexpr std::string_view kStreamField = "stream";
constexpr std::string_view kFormatField = "format";
constexpr std::string_view kRateField = "rate";
constexpr std::string_view kOutcomeField = "outcome";
constexpr std::string_view kCodeField = "code";
constexpr std::string_view kMessageField = "message";

// Logged when a frame lacks the field.
constexpr std::string_view kNoType = "";
constexpr int64_t kNoStream = -1;
constexpr int64_t kNoRate = 0;

int length_of(const hg::json::Value& message, std::string_view key) {
  return static_cast<int>(message[key].as_string().size());
}

}  // namespace

void log_turn_message(std::string_view frame) {
  hg::json::Value message;
  if (!hg::json::parse(frame, message)) return;  // the core reports malformed frames
  const std::string_view type = message[kTypeField].str_or(kNoType);
  if (type == kTurnStart) {
    ESP_LOGI(TAG, "started");
  } else if (type == kTranscript) {
    ESP_LOGI(TAG, "transcript: %d chars", length_of(message, kTextField));
  } else if (type == kReply) {
    ESP_LOGI(TAG, "reply: %d chars%s", length_of(message, kTextField),
             message[kInterimField].as_bool() ? " (interim)" : "");
  } else if (type == kAudioStart) {
    ESP_LOGI(TAG, "audio stream %d: %s at %d Hz", static_cast<int>(message[kStreamField].as_int(kNoStream)),
             message[kFormatField].as_string().c_str(), static_cast<int>(message[kRateField].as_int(kNoRate)));
  } else if (type == kAudioEnd || type == kAudioAbort) {
    ESP_LOGI(TAG, "%.*s, stream %d", static_cast<int>(type.size()), type.data(),
             static_cast<int>(message[kStreamField].as_int(kNoStream)));
  } else if (type == kTurnEnd) {
    ESP_LOGI(TAG, "ended: %s", message[kOutcomeField].as_string().c_str());
  } else if (type == kNotice) {
    ESP_LOGI(TAG, "notice: %s", message[kTextField].as_string().c_str());
  } else if (type == kError) {
    ESP_LOGW(TAG, "error %s: %s", message[kCodeField].as_string().c_str(),
             message[kMessageField].as_string().c_str());
  }
}

}  // namespace nabu
