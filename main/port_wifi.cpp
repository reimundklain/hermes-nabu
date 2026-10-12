// Station changes run on the app task. IDF callbacks only post link events.
#include "port.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

namespace hgp {
namespace {
uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Mains powered: the retry interval never backs off.
constexpr bool kOnBattery = false;
}

void Wifi::begin(NvsStorage& storage) {
  storage_ = &storage;
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  // Credentials live in our NVS store, not in the Wi-Fi driver's.
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Wifi::on_event, this));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Wifi::on_event, this));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
#if CONFIG_PM_ENABLE
  if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "hg-wifi-join", &attempt_lock_) != ESP_OK) attempt_lock_ = nullptr;
#endif
  ESP_ERROR_CHECK(esp_wifi_start());
}

void Wifi::hold_while_attempting() {
  const bool want = retry_.attempting();
  if (!attempt_lock_ || want == attempt_held_) return;
  attempt_held_ = want;
  if (want) esp_pm_lock_acquire(attempt_lock_);
  else esp_pm_lock_release(attempt_lock_);
}

void Wifi::join(const char* ssid, const char* password) {
  configured_ = false;
  retry_.connected();  // nothing pending until this join is under way
  hold_while_attempting();
  esp_wifi_disconnect();
  const size_t ssid_size = std::strlen(ssid), pass_size = std::strlen(password);
  if (!ssid_size || ssid_size > 32 || pass_size > 64) {
    events::post(EventType::NetDown, "Wi-Fi not configured", 20);
    return;
  }
  wifi_config_t cfg = {};
  std::memcpy(cfg.sta.ssid, ssid, ssid_size);
  std::memcpy(cfg.sta.password, password, pass_size);
  cfg.sta.threshold.authmode = pass_size ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  cfg.sta.pmf_cfg.capable = true;
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) return;
  configured_ = true;
  events::post(EventType::NetDown, "Joining Wi-Fi", 13);
  retry_.started(now_ms());
  if (esp_wifi_connect() != ESP_OK) retry_.start_failed(now_ms());
  hold_while_attempting();
}

void Wifi::reconfigure() {
  const std::string ssid = storage_->get("wifi_ssid").value_or(CONFIG_HG_DEFAULT_WIFI_SSID);
  const std::string pass = storage_->get("wifi_pass").value_or(CONFIG_HG_DEFAULT_WIFI_PASSWORD);
  join(ssid.c_str(), pass.c_str());
}

void Wifi::disconnected() {
  if (configured_) retry_.lost(now_ms(), kOnBattery);
  hold_while_attempting();
}

void Wifi::connected() {
  retry_.connected();
  hold_while_attempting();
}

void Wifi::tick(uint32_t now) {
  if (configured_ && retry_.due(now) && esp_wifi_connect() != ESP_OK) retry_.start_failed(now);
  hold_while_attempting();
}

void Wifi::on_event(void*, const char* base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    events::post(EventType::WifiStarted);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    auto* info = static_cast<wifi_event_sta_disconnected_t*>(data);
    char detail[48];
    std::snprintf(detail, sizeof(detail), "Wi-Fi lost (reason %d)", info ? info->reason : 0);
    events::post(EventType::WifiDisconnected, detail, std::strlen(detail));
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* got = static_cast<ip_event_got_ip_t*>(data);
    char detail[32];
    std::snprintf(detail, sizeof(detail), IPSTR, IP2STR(&got->ip_info.ip));
    ESP_LOGI("hg.wifi", "connected, ip %s", detail);
    events::post(EventType::NetUp, detail, std::strlen(detail));
  }
}

}  // namespace hgp
