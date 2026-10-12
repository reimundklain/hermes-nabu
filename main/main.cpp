// hermes-nabu entry point: wires the Voice PE drivers to the Hermes Gadget
// core (hg::App) and runs the app loop on the main task.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <string>
#include <string_view>

#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "led_ring.hpp"
#include "mute_switch.hpp"
#include "aic3204.hpp"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "turn_log.hpp"
#include "xmos_control.hpp"
#include "xmos_mic.hpp"
#include "xmos_speaker.hpp"

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
constexpr TickType_t kNoWait = 0;

// Storage keys of the core's settings that change how Wi-Fi joins.
constexpr std::string_view kWifiSsidKey = "wifi_ssid";
constexpr std::string_view kWifiPassKey = "wifi_pass";

// Reasons handed to the core when the connection goes away.
constexpr const char* kClosedReason = "closed";
constexpr const char* kLostReason = "disconnected";

// The core's console command behind Hermes /new.
constexpr std::string_view kNewSessionCommand = "new-session";

hgp::EspSystem g_system;
hgp::NvsStorage g_storage;
hgp::WsTransport g_transport;
hgp::Wifi g_wifi;
hgp::Buttons g_buttons;
nabu::LedRing g_ring;
nabu::MuteSwitch g_mute;
nabu::XmosControl g_xmos;
nabu::XmosMic g_mic;
nabu::Aic3204 g_dac;
nabu::XmosSpeaker g_speaker;
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

// The bus to the XMOS and the DAC.
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

// The XMOS boots while Wi-Fi starts; configure() waits for what is left.
// The DAC is set up only after that, in case the XMOS touches it while booting.
void init_audio(hg::Hal& hal) {
  i2c_master_bus_handle_t bus = init_internal_i2c();
  if (!bus) {
    ESP_LOGW(TAG, "running without audio");
    return;
  }
  const bool xmos_ready = g_xmos.configure(bus) == ESP_OK;
  const bool dac_started = g_dac.begin(bus) == ESP_OK;
  if (xmos_ready && g_mic.begin()) hal.mic = &g_mic;
  else ESP_LOGW(TAG, "running without the microphone");
  if (dac_started && g_dac.power_up() == ESP_OK && g_speaker.begin(g_dac)) hal.speaker = &g_speaker;
  else ESP_LOGW(TAG, "running without the speaker");
}

// Muting ends the conversation: an utterance under way is discarded, a reply
// stops, and the next turn starts in a fresh Hermes session.
void on_mute_changed(hg::App& app) {
  const bool muted = g_mute.muted();
  g_mic.set_muted(muted);
  ESP_LOGI(TAG, "microphone %s", muted ? "muted" : "unmuted");
  if (!muted) return;
  if (app.model().screen == hg::Screen::Listening) {
    app.on_button(hg::Button::Cancel, true);
    app.on_button(hg::Button::Cancel, false);
  }
  const std::string reply = app.console(kNewSessionCommand);
  ESP_LOGI(TAG, "conversation ended: %s", reply.c_str());
}

nabu::RingFrame ring_now(const hg::UiModel& model) {
  nabu::RingFrame frame = nabu::ring_frame(model.screen, g_system.now_ms());
  if (g_mute.muted()) nabu::mark_muted(frame);
  return frame;
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
    case EventType::WsText:
      nabu::log_turn_message(std::string_view(text, ev.len));
      app.on_transport_text(std::string_view(text, ev.len));
      break;
    case EventType::WsBinary: app.on_transport_binary(ev.data, ev.len); break;
    case EventType::WsClosed: app.on_transport_closed(text ? text : kClosedReason); break;
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

  g_mute.begin();
  g_mic.set_muted(g_mute.muted());

  hgp::ButtonConfig buttons;
  buttons.talk = kCenterButtonGpio;
  g_buttons.begin(buttons);

  // Wi-Fi first: the radio is the entropy source for the device key.
  g_wifi.begin(g_storage);

  hg::Hal hal;
  hal.system = &g_system;
  hal.transport = &g_transport;
  hal.storage = &g_storage;
  init_audio(hal);

  hg::DeviceProfile profile;
  profile.board = kBoardName;
  profile.firmware = version;
  profile.default_name = CONFIG_HG_DEFAULT_NAME;
  profile.default_server_url = CONFIG_HG_DEFAULT_SERVER_URL;
  profile.default_access_token = CONFIG_HG_DEFAULT_ACCESS_TOKEN;
  profile.has_cancel_button = false;
  profile.has_scroll_buttons = false;
  profile.mic_rate = nabu::XmosMic::kRate;
  profile.speaker_rate = nabu::XmosSpeaker::kRate;

  static hg::App app(hal, profile);
  app.on_setting_changed = [](std::string_view key) {
    if (key == kWifiSsidKey || key == kWifiPassKey) g_wifi.reconfigure();
  };
  app.begin();
  // Without a USB host, the console's reads fail at once and its REPL task
  // spins above this task's priority, so the main loop would never run. USB-C
  // is the only power input, so plugging in a host restarts the device anyway.
  if (usb_serial_jtag_is_connected()) {
    hgp::console::begin();
  } else {
    ESP_LOGI(TAG, "no USB host, serial console off");
  }

  for (;;) {
    hgp::Event ev;
    if (hgp::events::receive(ev, kEventWait)) {
      do {
        dispatch(app, ev);
        hgp::events::release(ev);
      } while (hgp::events::receive(ev, kNoWait));
    }
    // A loss whose WsClosed event was dropped; harmless if it was delivered too.
    if (g_transport.take_lost()) app.on_transport_closed(kLostReason);
    if (g_mute.poll()) on_mute_changed(app);
    g_buttons.poll(app);
    g_wifi.tick(g_system.now_ms());
    app.tick();
    log_screen_change(app.model());
    log_hint_change(app.model());
    g_ring.show(ring_now(app.model()));
  }
}
