# hermes-nabu

Thin-client firmware for the
[Home Assistant Voice Preview Edition](https://github.com/esphome/home-assistant-voice-pe)
(ESP32-S3), without Home Assistant.

The device only handles audio I/O and local controls (button, LED ring,
mute). It streams microphone audio to Hermes over a WebSocket and plays back
the audio it receives. Speech-to-text, the assistant itself, and
text-to-speech all run on the host.

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

The console runs over the USB-C port (USB Serial/JTAG), so the device shows
up as `/dev/ttyACM*`. Leave `idf.py monitor` with `Ctrl+]`.

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

| Path                 | Purpose                                         |
| -------------------- | ----------------------------------------------- |
| `flake.nix`          | Dev shell (ESP-IDF, toolchain, formatter)       |
| `CMakeLists.txt`     | ESP-IDF project definition                      |
| `main/`              | Application component                           |
| `sdkconfig.defaults` | Board settings (target, flash, PSRAM, console)  |

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
- [ ] Wi-Fi connection
- [ ] WebSocket connection to the Hermes gadget endpoint, send `hello` frame
- [ ] Push-to-talk on the center button, status on the LED ring
- [ ] Mic capture, stream as PCM16 chunks
- [ ] Speaker playback
- [ ] XMOS initialization for better audio quality

The gadget protocol (JSON control frames and binary audio frames over a
WebSocket) is documented in `docs/protocol.md` of the Hermes Gadget SDK.
