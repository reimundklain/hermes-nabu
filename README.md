# hermes-nabu

> [!IMPORTANT]
> **Unofficial community project.** hermes-nabu is not affiliated with,
> endorsed by, or supported by Nabu Casa, the Open Home Foundation, Home
> Assistant, ESPHome, or Nous Research. "Home Assistant" is a trademark of the
> Open Home Foundation, "Hermes" and "Hermes Agent" are trademarks of Nous
> Research. These names are used only to describe the hardware and software
> this firmware works with.

Unofficial thin-client firmware for the
[Home Assistant Voice Preview Edition](https://github.com/esphome/home-assistant-voice-pe)
(ESP32-S3), without Home Assistant.

The device only handles audio I/O and local controls (button, LED ring,
mute). It streams microphone audio to Hermes over a WebSocket and plays back
the audio it receives. Speech-to-text, the assistant itself, and
text-to-speech all run on the host.

The protocol side (handshake, pairing, actions, heartbeat, OTA) is the
portable C++17 core of the
[Hermes Gadget SDK](https://github.com/Adolanium/hermes-gadget-sdk), pulled in
as an ESP-IDF component and pinned to an exact commit. This repository holds
the Voice PE port: the ESP32 drivers the core runs on, plus the board-specific
parts (LED ring, XMOS, mute switch, dial).

## Requirements

- [Nix](https://nixos.org) with flakes enabled
- Optional: [direnv](https://direnv.net) for automatic shell activation

The flake provides ESP-IDF v5.5.2 and the Xtensa toolchain via
[nixpkgs-esp-dev](https://github.com/mirrexagon/nixpkgs-esp-dev).
Nothing needs to be installed globally.

## Getting started

```sh
direnv allow        # or: nix develop
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The first build downloads the pinned components (Gadget core,
`esp_websocket_client`) into `managed_components/`.

The console runs over the USB-C port (USB Serial/JTAG), so the device shows
up as `/dev/ttyACM*`. Leave `idf.py monitor` with `Ctrl+]`.

## Configuration

Wi-Fi and the Hermes endpoint are set over the serial console and stored in
NVS. Nothing is compiled in:

```text
gadget> set wifi_ssid MyWifi
gadget> set wifi_pass secret
gadget> set server ws://<hermes-host>:8765/gadget
gadget> status
```

`hermes gadget info` on the host prints the endpoint. When the device first
connects, `status` shows a pairing code; approve it on the host with
`hermes pairing approve gadget <code>`. `help` lists all commands.

For development builds, `idf.py menuconfig` → *Hermes Nabu* sets defaults
that NVS values override. They end up in the untracked `sdkconfig`.

Run `nix fmt` to format the Nix files.

## Recovery / bootloader mode

You can always reflash the device, including back to the stock firmware:

1. Unplug power.
2. Hold the center button.
3. Connect USB-C to the computer.
4. Release the button after a moment.
5. Flash with `idf.py flash`, or restore the stock firmware with the
   [official web installer](https://esphome.github.io/home-assistant-voice-pe/)
   (Chrome/Edge).

## Project layout

| Path                     | Purpose                                        |
| ------------------------ | ---------------------------------------------- |
| `flake.nix`              | Dev shell (ESP-IDF, toolchain, formatter)      |
| `CMakeLists.txt`         | ESP-IDF project definition                     |
| `main/main.cpp`          | Entry point: wires drivers to `hg::App`        |
| `main/port*`             | ESP32 HAL drivers (Wi-Fi, WebSocket, NVS, …)   |
| `main/led_*`             | LED ring driver and per-state patterns         |
| `main/xmos_*`            | XMOS control, mic in and speaker out over I2S  |
| `main/aic3204.*`         | Speaker DAC setup and volume                   |
| `main/mute_switch.*`     | Hardware mute switch                           |
| `main/idf_component.yml` | Component dependencies, Gadget core pin        |
| `dependencies.lock`      | Resolved component versions                    |
| `sdkconfig.defaults`     | Board settings (target, flash, PSRAM, console) |
| `LICENSE`                | MIT license of this project                    |
| `NOTICE`                 | Third-party code, upstream credits, trademarks |

`sdkconfig` is generated from `sdkconfig.defaults` during the build and is
not tracked. Put persistent configuration changes into `sdkconfig.defaults`.

## Hardware

ESP32-S3, 16 MB flash, octal PSRAM. Pin mapping taken from the upstream
[`home-assistant-voice.yaml`](https://github.com/esphome/home-assistant-voice-pe/blob/dev/home-assistant-voice.yaml):

| Function                          | GPIO         |
| --------------------------------- | ------------ |
| Mic I2S in (BCLK/LRCLK/DIN)       | 13 / 14 / 15 |
| Speaker I2S out (BCLK/LRCLK/DOUT) | 8 / 7 / 10   |
| Speaker amplifier enable          | 47           |
| I2C to XMOS (SDA/SCL)             | 5 / 6        |
| XMOS reset                        | 4            |
| Center button                     | 0            |
| Hardware mute switch              | 3            |
| Rotary dial (A/B)                 | 16 / 18      |
| LED ring (12× GRB)                | 21           |
| LED power                         | 45           |
| Audio jack detect                 | 17           |

The XMOS audio processor (echo cancellation, beamforming) is controlled over
I2C. See `esphome/components/voice_kit/` upstream for its protocol and DFU.

## Roadmap

Each step gets flashed and tested on the device before moving on.

- [x] Minimal app that logs over serial
- [x] Gadget core integrated, Wi-Fi and WebSocket transport ported
- [x] On device: join Wi-Fi, connect to Hermes (`ws://` and `wss://`), pair
- [x] Status on the LED ring, center button wired to Talk
- [x] Mic capture through the XMOS, push-to-talk on the center button
- [x] Speaker playback through the XMOS and the AIC3204 DAC
- [x] XMOS initialization (echo cancellation, noise suppression, gain control)
- [x] Hardware mute switch: no capture while muted, red marks on the ring
- [ ] OTA with a two-slot partition table and rollback
- [ ] On-device wake word (microWakeWord on TFLite Micro, fed
      from XMOS channel 1). Open: tap mode vs. a core API to start a
      hands-free turn; detection while a reply plays

The gadget protocol (JSON control frames and binary audio frames over a
WebSocket) is documented in `docs/protocol.md` of the Hermes Gadget SDK.

## License

MIT, see `LICENSE`. Code taken from the Hermes Gadget SDK keeps its MIT
license; `NOTICE` lists those files and credits the upstream projects the
hardware details come from.
