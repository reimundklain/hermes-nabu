// hermes-nabu entry point: wires the Voice PE drivers to the Hermes Gadget
// core (hg::App) and runs the app loop on the main task.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <string>

#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "led_ring.hpp"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "xmos_control.hpp"
#include "xmos_mic.hpp"

namespace {

const char* TAG = "nabu.main";
constexpr const char* kBoardName = "voice-pe";
constexpr int kCenterButtonGpio = 0;
constexpr gpio_num_t kI2cSdaPin = GPIO_NUM_5;
constexpr gpio_num_t kI2cSclPin = GPIO_NUM_6;
// ESP-IDF's usual filter: ignore glitches shorter than 7 APB clock cycles.
constexpr uint8_t kI2cGlitchFilter = 7;
// Block this long for events, then run the core's timers.
constexpr TickType_t kEventWait = pdMS_TO_TICKS(10);

hgp::EspSystem g_system;
hgp::NvsStorage g_storage;
hgp::WsTransport g_transport;
hgp::Wifi g_wifi;
hgp::Buttons g_buttons;
nabu::LedRing g_ring;
nabu::XmosControl g_xmos;
nabu::XmosMic g_mic;
hg::Screen g_logged_screen = hg::Screen::Boot;
std::string g_logged_hint;

// Without a display, the serial log is where the pairing code shows up.
void log_screen_change(const hg::UiModel& model) {
  if (model.screen == g_logged_screen) return;
  g_logged_screen = model.screen;
  if (model.screen == hg::Screen::Pairing) {
    ESP_LOGI(TAG, "state: pairing, code %s (approve: hermes pairing approve gadget %s)", model.code.c_str(),
             model.code.c_str());
    return;
  }
  ESP_LOGI(TAG, "state: %s", hg::screen_name(model.screen));
}

// The core's hints ("Microphone unavailable", "Approve pairing first") are
// meant for a screen; the log is the closest thing here.
void log_hint_change(const hg::UiModel& model) {
  if (model.hint == g_logged_hint) return;
  g_logged_hint = model.hint;
  if (!model.hint.empty()) ESP_LOGI(TAG, "hint: %s", model.hint.c_str());
}

// The bus to the XMOS (and later the speaker DAC).
i2c_master_bus_handle_t init_internal_i2c() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = kI2cSdaPin;
  cfg.scl_io_num = kI2cSclPin;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = kI2cGlitchFilter;
  cfg.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  const esp_err_t err = i2c_new_master_bus(&cfg, &bus);
  if (err != ESP_OK) ESP_LOGE(TAG, "I2C bus failed: %s", esp_err_to_name(err));
  return bus;
}

// The XMOS boots while Wi-Fi starts; configure() waits for whatever is left.
bool init_microphone() {
  i2c_master_bus_handle_t bus = init_internal_i2c();
  if (!bus || g_xmos.configure(bus) != ESP_OK) return false;
  return g_mic.begin();
}

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
    case EventType::Mic:
      app.on_mic_samples(reinterpret_cast<const int16_t*>(ev.data), ev.len / sizeof(int16_t));
      break;
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

  if (g_xmos.reset() != ESP_OK) ESP_LOGW(TAG, "XMOS reset failed");
  if (!g_ring.begin()) ESP_LOGW(TAG, "running without the LED ring");
  g_ring.show(nabu::ring_frame(hg::Screen::Boot, g_system.now_ms()));

  hgp::ButtonConfig buttons;
  buttons.talk = kCenterButtonGpio;
  g_buttons.begin(buttons);

  // Wi-Fi first: the radio is the entropy source for the device key.
  g_wifi.begin(g_storage);

  hg::Hal hal;
  hal.system = &g_system;
  hal.transport = &g_transport;
  hal.storage = &g_storage;
  if (init_microphone()) hal.mic = &g_mic;
  else ESP_LOGW(TAG, "running without the microphone");

  hg::DeviceProfile profile;
  profile.board = kBoardName;
  profile.firmware = version;
  profile.default_name = CONFIG_HG_DEFAULT_NAME;
  profile.default_server_url = CONFIG_HG_DEFAULT_SERVER_URL;
  profile.default_access_token = CONFIG_HG_DEFAULT_ACCESS_TOKEN;
  profile.has_cancel_button = false;
  profile.has_scroll_buttons = false;
  profile.mic_rate = nabu::XmosMic::kRate;

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
    g_buttons.poll(app);
    g_wifi.tick(g_system.now_ms());
    app.tick();
    log_screen_change(app.model());
    log_hint_change(app.model());
    g_ring.show(nabu::ring_frame(app.model().screen, g_system.now_ms()));
  }
}
