#include "turn_log.hpp"

#include "esp_log.h"
#include "hg/json.hpp"

namespace nabu {
namespace {

const char* TAG = "nabu.turn";

int length_of(const hg::json::Value& message, std::string_view key) {
  return static_cast<int>(message[key].as_string().size());
}

}  // namespace

void log_turn_message(std::string_view frame) {
  hg::json::Value message;
  if (!hg::json::parse(frame, message)) return;  // the core reports malformed frames
  const std::string_view type = message["type"].str_or("");
  if (type == "turn.start") {
    ESP_LOGI(TAG, "started");
  } else if (type == "transcript") {
    ESP_LOGI(TAG, "transcript: %d chars", length_of(message, "text"));
  } else if (type == "reply") {
    ESP_LOGI(TAG, "reply: %d chars%s", length_of(message, "text"),
             message["interim"].as_bool() ? " (interim)" : "");
  } else if (type == "audio.start") {
    ESP_LOGI(TAG, "audio stream %d: %s at %d Hz", static_cast<int>(message["stream"].as_int(-1)),
             message["format"].as_string().c_str(), static_cast<int>(message["rate"].as_int(0)));
  } else if (type == "audio.end" || type == "audio.abort") {
    ESP_LOGI(TAG, "%.*s, stream %d", static_cast<int>(type.size()), type.data(),
             static_cast<int>(message["stream"].as_int(-1)));
  } else if (type == "turn.end") {
    ESP_LOGI(TAG, "ended: %s", message["outcome"].as_string().c_str());
  } else if (type == "notice") {
    ESP_LOGI(TAG, "notice: %s", message["text"].as_string().c_str());
  } else if (type == "error") {
    ESP_LOGW(TAG, "error %s: %s", message["code"].as_string().c_str(), message["message"].as_string().c_str());
  }
}

}  // namespace nabu
