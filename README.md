# ESP32-S3 Remote PC Controller

Firmware for an ESP32-S3 that sits inside a desktop PC and provides:

- Safe remote power / long-hold / reset (relay pulses with hard timeouts)
- PC on/off sensing (PC817)
- Authenticated local HTTP API
- USB HID keyboard (stub → real TinyUSB next)
- Matter / Google Home short power pulse (stub → Espressif Matter next)

**You do not need to design the firmware.** Tell me the features you want; I handle pins, safety, and code.

## Board locked in

- Module: **ESP32-S3-WROOM-1-N16R8** (16MB flash, 8MB PSRAM)
- Dual USB-C DevKit (Serial/JTAG + native USB)

Provisional wiring (changeable later without rewriting logic):

| Function         | GPIO |
| ---------------- | ---- |
| POWER relay      | 4    |
| RESET relay      | 5    |
| PC state (PC817) | 6    |

Relays assumed **active-low** (HIGH = off).

## Safety rules already in the code

- Relays forced OFF at boot
- No permanent “relay on” API
- ESP32 owns pulse timing (max **8 seconds**)
- Independent relay watchdog
- POWER and RESET cannot run at the same time
- Emergency `POST /api/pc/release`
- Matter path (when enabled) may only call short `powerPress()`

## What you need to do (simple)

1. Keep the board nearby with a USB-C cable.
2. Tell me your **Wi-Fi name** when you’re ready for network tests (I’ll configure it securely; it won’t go into a public git commit).
3. When we wire hardware: I’ll give you **step-by-step “plug wire A into pin B”** instructions — no electronics theory required.
4. Never connect relays to the motherboard until I’ve said the firmware passed dry tests.

## Developer machine setup (I can do this for you)

Requires Espressif ESP-IDF for ESP32-S3. After IDF is installed and `export.sh` is sourced:

```bash
cd /Users/shivanshtyagi/Codebase/esp32-pc-controller
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

Configure Wi-Fi / API token (optional until network stage):

```bash
idf.py menuconfig
# → ESP32 PC Controller
```

## Local API (after Wi-Fi works)

All routes require header:

```http
Authorization: Bearer <your-token>
```

| Method | Path                 | Action                             |
| ------ | -------------------- | ---------------------------------- |
| GET    | `/api/status`        | Status JSON                        |
| POST   | `/api/pc/power`      | 500 ms power press                 |
| POST   | `/api/pc/power/hold` | Long hold (`{"duration_ms":5000}`) |
| POST   | `/api/pc/reset`      | 500 ms reset                       |
| POST   | `/api/pc/release`    | Emergency release                  |
| POST   | `/api/hid/key`       | `{"key":"enter"}` etc.             |

## Build status

Scaffolded for bring-up. Next: install ESP-IDF on this Mac, flash a blink/safe-boot build, then dry-test relays before any motherboard connection.

## License

ESP32-PC-controller — including **all past and present commits** in this repository — is
licensed under the [GNU General Public License v3.0](LICENSE), with
[Additional Terms](NOTICE) under GPL §7.

In short:

- Derivative works and redistributed copies must remain open source under GPL-3.0.
- Products built with LumosOS must give clear front-page credit that LumosOS was used to build them (see `NOTICE`).

```
Copyright (C) 2026 Shivansh Tyagi
```
