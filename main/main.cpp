// hermes-nabu entry point: wires the Voice PE drivers to the Hermes Gadget
// core (hg::App) and runs the app loop on the main task.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

const char* TAG = "nabu.main";
constexpr const char* kBoardName = "voice-pe";
// Block this long for events, then run the core's timers.
constexpr TickType_t kEventWait = pdMS_TO_TICKS(10);

hgp::EspSystem g_system;
hgp::NvsStorage g_storage;
hgp::WsTransport g_transport;
hgp::Wifi g_wifi;

void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

void dispatch(hg::App& app, hgp::Event& ev) {
  using hgp::EventType;
  const char* text = reinterpret_cast<const char*>(ev.data);
  // Frames from a connection the app has already abandoned are dropped.
  bool stale = (ev.type == EventType::WsOpen || ev.type == EventType::WsText || ev.type == EventType::WsBinary ||
                ev.type == EventType::WsClosed) &&
               ev.generation != g_transport.generation();
  if (stale) return;
  switch (ev.type) {
    case EventType::NetUp:
      g_wifi.connected();
      app.on_network(true, text ? text : "");
      break;
    case EventType::NetDown:
      app.on_network(false, text ? text : "");
      break;
    case EventType::WifiDisconnected:
      g_wifi.disconnected();
      app.on_network(false, text ? text : "");
      break;
    case EventType::WifiStarted: g_wifi.reconfigure(); break;
    case EventType::WsOpen: app.on_transport_open(); break;
    case EventType::WsText: app.on_transport_text(std::string_view(text, ev.len)); break;
    case EventType::WsBinary: app.on_transport_binary(ev.data, ev.len); break;
    case EventType::WsClosed: app.on_transport_closed(text ? text : "closed"); break;
    case EventType::Console:
      ev.console->answer(app.console(std::string_view(text ? text : "", ev.len)));
      break;
  }
}

}  // namespace

extern "C" void app_main(void) {
  init_nvs();
  hgp::events::init();
  ESP_ERROR_CHECK(g_storage.begin() ? ESP_OK : ESP_FAIL);
  const char* version = esp_app_get_description()->version;
  ESP_LOGI(TAG, "hermes-nabu %s on %s", version, kBoardName);

  // Wi-Fi first: the radio is the entropy source for the device key.
  g_wifi.begin(g_storage);

  hg::Hal hal;
  hal.system = &g_system;
  hal.transport = &g_transport;
  hal.storage = &g_storage;

  hg::DeviceProfile profile;
  profile.board = kBoardName;
  profile.firmware = version;
  profile.default_name = CONFIG_HG_DEFAULT_NAME;
  profile.default_server_url = CONFIG_HG_DEFAULT_SERVER_URL;
  profile.default_access_token = CONFIG_HG_DEFAULT_ACCESS_TOKEN;
  profile.has_cancel_button = false;
  profile.has_scroll_buttons = false;

  static hg::App app(hal, profile);
  app.on_setting_changed = [](std::string_view key) {
    if (key == "wifi_ssid" || key == "wifi_pass") g_wifi.reconfigure();
  };
  app.begin();
  hgp::console::begin();

  for (;;) {
    hgp::Event ev;
    if (hgp::events::receive(ev, kEventWait)) {
      do {
        dispatch(app, ev);
        hgp::events::release(ev);
      } while (hgp::events::receive(ev, 0));
    }
    // A loss whose WsClosed event was dropped; harmless if it was delivered too.
    if (g_transport.take_lost()) app.on_transport_closed("disconnected");
    g_wifi.tick(g_system.now_ms());
    app.tick();
  }
}
