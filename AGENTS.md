# AGENTS.md

Guidance for AI agents working on the Hermes Voice PE Satellite firmware.

## Project Overview

This project creates a dedicated firmware that turns the **Home Assistant Voice Preview Edition** hardware into a satellite for **Hermes Agent**.

- Hardware: ESP32-S3 (16 MB flash, 8 MB octal PSRAM), XMOS XU316 audio DSP, LED ring, center button, mute switch, 3.5 mm jack, internal speaker.
- Goal: Push-to-talk (or wake-word) voice satellite that streams audio to Hermes and plays replies, while exposing LEDs / button / sensors as Hermes tools.
- Preferred base: Port or adapt the Hermes Gadget SDK (https://github.com/Adolanium/hermes-gadget-sdk) rather than starting from scratch or from the stock ESPHome Voice PE YAML.
- Protocol: Hermes Gadget protocol v1 (WebSocket + JSON text frames + binary PCM audio). See the protocol documentation in the Gadget SDK.

Hermes Agent itself (and its Gadget plugin) runs on a host (laptop / Pi / server). The Voice PE only handles audio I/O, UI (LED ring), pairing, and local actions.

## Architecture

- Standalone project on the shared C++17 core from hermes-gadget-sdk (`firmware/core`), consumed as an ESP-IDF managed component pinned to an exact commit in `main/idf_component.yml`. Never edit `managed_components/`; bump the pin deliberately.
- The port lives in `main/`: generic ESP32 drivers copied from the SDK's `firmware/esp32/main` (keep them close to upstream, record every copy or change in `NOTICE`) plus Voice PE-specific drivers.
- The App reaches the hardware only through the `hg::Hal` interfaces; driver tasks post events, and only the main loop calls into `hg::App`.
- Audio path should leverage the XMOS chip (I2C control + I2S) for clean AEC/NS/AGC instead of raw I2S if possible.
- LED ring shows states: ready, listening, thinking, speaking, error, pairing.
- Center button: hold-to-talk (and confirm prompts). Mute switch should be respected.
- Device actions: LED color/animation control, volume, mute state, etc., exposed via the Gadget `actions` manifest.

## Build & Development

- Target: ESP32-S3, ESP-IDF v5.5.2 pinned by `flake.nix` (the SDK core needs ≥ 5.3).
- Use the project’s ESP-IDF environment and build commands.
- Prefer `idf.py` workflows.
- Never edit generated files or `sdkconfig` directly — use `sdkconfig.defaults`.
- Document exact build / flash / monitor commands in the README.
- Host-side tests (protocol vectors, unit tests) should pass before hardware tests.

## Code Conventions

Follow ESP-IDF and the Hermes Gadget SDK style:

- C++17, no exceptions.
- Naming: `snake_case` for functions/variables, `PascalCase` for types, trailing `_` for private members.
- Prefer `this->` for member access.
- 2- or 4-space indentation (match existing core).
- No magic strings or numbers: bind pins, register addresses and values, timeouts, sizes, protocol message types and field names, setting keys and task names to a named `constexpr` or `enum class` in the narrowest scope that fits. Exceptions: obvious tautologies (`0`, `1`, `""`), GPIO on/off levels, and register setup tables whose rows are keyed by a named register and commented. Files copied from the SDK stay as upstream wrote them.
- Logging: `ESP_LOGI` / `ESP_LOGW` / `ESP_LOGE` with a clear TAG.
- Explicit `esp_err_t` checking.
- No heap allocation in ISRs.
- Respect FreeRTOS task priorities and avoid long blocking calls in the main loop.
- Audio: 16 kHz mono PCM16 is the common rate used by the Gadget protocol. Declare it correctly in `hello.caps`.

## Protocol & Integration Rules

- Implement the Hermes Gadget protocol v1 faithfully (hello / challenge / auth / welcome, audio.start/end, reply streaming, actions, prompts, OTA).
- Device ID is derived from a persistent 32-byte key stored in NVS.
- Advertise accurate capabilities (`mic`, `speaker`, `inputs`, optional `display` if you add a face, `actions`).
- Answer every `action` exactly once.
- Handle pairing flow and show the pairing code clearly (LED pattern or serial).
- Respect `audio.abort` and `cancel`.
- Heartbeat / ping-pong must be implemented.

Reference: https://github.com/Adolanium/hermes-gadget-sdk/blob/main/docs/protocol.md

## Safety & Boundaries

- Never hardcode Wi-Fi credentials, Hermes tokens, or API keys.
- Ask before large protocol changes or changes that break compatibility with the Gadget plugin.
- Confirm before flashing real hardware.
- Preserve the ability to fall back / recover (OTA rollback, factory reset via button).
- Keep changes scoped. Do not mix unrelated refactors.

## Validation

- Protocol compliance against the Gadget SDK test vectors.
- Successful build for the Voice PE board profile.
- End-to-end: hold button → audio reaches Hermes → reply audio plays, LEDs update.
- Pairing, reconnect, and OTA flows.
- Mute switch and jack detection behave correctly.

## References

- Hermes Gadget SDK: https://github.com/Adolanium/hermes-gadget-sdk
- Protocol: docs/protocol.md in the Gadget SDK
- Porting guide: docs/porting.md
- Hermes Agent: https://github.com/NousResearch/hermes-agent
- Voice PE hardware docs: https://voice-pe.home-assistant.io/

Update this file when board support, protocol usage, or build process changes.
