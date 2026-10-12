// ESP32 implementations of the core HAL, plus the event queue that moves
// driver events (Wi-Fi, WebSocket, console) onto the app task.
//
// Trimmed from hermes-gadget-sdk firmware/esp32/main/port.hpp to the parts
// the Voice PE uses (ButtonConfig from board.hpp); see NOTICE.
//
// Threading rule: hg::App is only touched by the app task (app_main's loop).
// Driver tasks and callbacks post Events; the loop dispatches them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "freertos/FreeRTOS.h"  // must precede every other FreeRTOS header

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_websocket_client.h"
#include "freertos/semphr.h"
#include "hg/app.hpp"
#include "hg/hal.hpp"
#include "shared_reply.hpp"
#include "wifi_retry.hpp"
#include "ws_link.hpp"

namespace hgp {

// ---------------------------------------------------------------------------
// Events

enum class EventType : uint8_t { NetUp, NetDown, WsOpen, WsText, WsBinary, WsClosed, Console, WifiStarted,
                                 WifiDisconnected };

// Wakes a console command when the app task has answered it.
struct ConsoleSignal {
  SemaphoreHandle_t handle = xSemaphoreCreateBinary();
  ~ConsoleSignal() {
    if (handle) vSemaphoreDelete(handle);
  }
  void give() {
    if (handle) xSemaphoreGive(handle);
  }
  bool wait(uint32_t timeout_ms) { return handle && xSemaphoreTake(handle, pdMS_TO_TICKS(timeout_ms)) == pdTRUE; }
};

// Shared by the console task and the app task; see shared_reply.hpp.
using ConsoleRequest = hg::SharedReply<ConsoleSignal>;

struct Event {
  EventType type;
  uint32_t generation;  // WebSocket connection generation (stale events are dropped)
  uint8_t* data;        // heap payload owned by the event, freed after dispatch
  size_t len;
  ConsoleRequest* console;
};

namespace events {
void init();
// Copies `data`. Returns false (and drops the event) when the queue is full.
bool post(EventType type, const void* data = nullptr, size_t len = 0, uint32_t generation = 0,
          ConsoleRequest* console = nullptr);
bool receive(Event& out, TickType_t wait);
void release(Event& ev);
}  // namespace events

// ---------------------------------------------------------------------------
// HAL implementations

class EspSystem final : public hg::System {
 public:
  uint32_t now_ms() override;
  void random_bytes(uint8_t* out, size_t len) override;
  void log(hg::LogLevel level, std::string_view message) override;
};

class NvsStorage final : public hg::Storage {
 public:
  bool begin();
  std::optional<std::string> get(std::string_view key) override;
  bool set(std::string_view key, std::string_view value) override;
  void erase(std::string_view key) override;

 private:
  uint32_t handle_ = 0;  // nvs_handle_t
  SemaphoreHandle_t lock_ = nullptr;  // the Wi-Fi task reads credentials too
};

class WsTransport final : public hg::Transport {
 public:
  void connect(const std::string& url, const std::string& subprotocol) override;
  bool send_text(std::string_view text) override;
  bool send_binary(const uint8_t* data, size_t len) override;
  void close() override;
  uint32_t generation() const { return gens_.current(); }
  // True once if the current connection was lost. Backs up the WsClosed event,
  // which the client may post from the app task itself while the queue is full.
  bool take_lost() { return gens_.take_lost(); }

 private:
  static void on_event(void* arg, const char* base, int32_t id, void* data);
  // Sends run on their own task, so a slow or dead link never stalls the app
  // task: send_*() only queue a copy.
  static void tx_task(void* arg);
  bool enqueue(uint8_t opcode, const void* data, size_t len);
  esp_websocket_client_handle_t client_ = nullptr;
  QueueHandle_t tx_ = nullptr;
  hg::ws::Generations gens_;
  std::string url_, subprotocol_;
  // Fragment reassembly, WebSocket task only: an old client's task can still be
  // delivering a frame after close(), so connect() leaves this alone. Each
  // message's first frame clears it.
  std::string rx_;
  uint8_t rx_opcode_ = 0;
};

struct ButtonConfig {
  int talk = -1, cancel = -1, up = -1, down = -1;  // active-low GPIOs, -1 = absent
};

class Buttons {
 public:
  void begin(const ButtonConfig& cfg);
  void poll(hg::App& app);  // call every ~10 ms from the app task

 private:
  struct Button {
    int gpio = -1;
    hg::Button id = hg::Button::Talk;
    bool pressed = false;
    uint8_t stable = 0;
  };
  Button buttons_[4];
};

// Station mode only. The SDK's phone setup network is left out: the Voice PE
// has no screen to show its credentials, so Wi-Fi is provisioned over serial.
class Wifi {
 public:
  void begin(NvsStorage& storage);
  void reconfigure();  // credentials changed through the console
  void disconnected();
  void connected();
  void tick(uint32_t now);

 private:
  static void on_event(void* arg, const char* base, int32_t id, void* data);
  void join(const char* ssid, const char* password);
  void hold_while_attempting();
  NvsStorage* storage_ = nullptr;
  bool configured_ = false;
  hg::WifiRetry retry_;
  esp_pm_lock_handle_t attempt_lock_ = nullptr;  // light sleep would stall an attempt
  bool attempt_held_ = false;
};

namespace console {
// Starts the serial console REPL; lines are executed by hg::App::console on the app task.
void begin();
}

}  // namespace hgp
