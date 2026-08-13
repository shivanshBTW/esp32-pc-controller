# WakeType

ESP32-S3 appliance firmware for remote PC power, reset, and USB HID keyboard control.

WakeType is **not** an operating system and is **not** LumosOS. It lives inside a desktop PC and safely pulses motherboard POWER/RESET switch headers, reports power state, and (later) types via native USB.

## Hardware

- ESP32-S3-WROOM-1-N16R8 (dual USB-C DevKit)
- 2-channel 5V optocoupler relay module (POWER + RESET, COM/NO in parallel with case buttons)
- PC817 optocoupler for POWER LED sense
- Always-powered PC USB port for the ESP32

Provisional GPIOs (change in `idf.py menuconfig` → WakeType):

| Function | GPIO |
|----------|------|
| POWER relay | 4 |
| RESET relay | 5 |
| PC state (PC817) | 6 |

Relays default to **active-low**.

## Safety

- Relays forced OFF at boot (before Wi‑Fi/HTTP)
- No permanent relay-on API
- Firmware owns pulse timing (max 8 s) + watchdog
- POWER/RESET mutual exclusion
- Emergency `POST /api/v1/pc/release`
- OTA forces relays OFF before flashing
- Local lock can block Matter/API remote commands; physical case buttons still work

## First boot (Wi‑Fi)

1. Flash firmware over USB (Serial/JTAG port).
2. Phone/laptop joins Wi‑Fi hotspot **`WakeType-Setup`**.
3. Open `http://192.168.4.1/` (captive portal may open automatically).
4. Scan → pick home Wi‑Fi → Save & connect.
5. Copy the API token from the status JSON into the token box (saved in the browser).
6. After join: open `http://waketype.local/` on your LAN.

If STA Wi‑Fi fails repeatedly, SoftAP opens again for recovery (important once the board is sealed in a PC).

## Build / flash

```bash
. $HOME/esp/esp-idf/export.sh
cd /path/to/esp32-pc-controller
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

## Local API (`/api/v1`)

Header (after setup):

```http
Authorization: Bearer <token>
```

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/v1/status` | Health, Wi‑Fi, PC state, relays, heap |
| GET | `/api/v1/wifi/scan` | Nearby SSIDs (setup UI) |
| POST | `/api/v1/wifi/connect` | `{"ssid","password"}` |
| GET/POST | `/api/v1/settings` | Hostname, static IP, timings, local lock |
| GET/POST | `/api/v1/config` | JSON backup/restore (secrets optional; clears static IP on import by default) |
| POST | `/api/v1/pc/power` | Short power press |
| POST | `/api/v1/pc/power/hold` | `{"duration_ms":5000}` |
| POST | `/api/v1/pc/reset` | Reset pulse |
| POST | `/api/v1/pc/release` | Emergency release |
| POST | `/api/v1/hid/key` | `{"key":"enter"}` (HID stub until TinyUSB) |
| POST | `/api/v1/ota` | Raw `.bin` upload (auth required) |

Web UI pages: `/` (status + controls + Wi‑Fi), `/settings`, `/ota`.

mDNS: `waketype.local`, service `_waketype._tcp`.

## Dry-test relays (before motherboard wiring)

With the board powered over USB only (relays **not** wired to the PC yet):

1. Join `WakeType-Setup` or LAN `waketype.local`.
2. Paste API token → **Refresh** — confirm `power_relay_active` / `reset_relay_active` are false.
3. Tap **Power** — relay module LED/click for ~500 ms, then off.
4. Tap **Long hold** — ~5 s then off (never leave ON).
5. Tap **Reset** — short pulse on channel 2.
6. Tap **Release** anytime — both off.
7. Confirm `pc_state` moves only when PC817 is wired; ignore it until then.

Do **not** connect COM/NO to motherboard headers until hundreds of dry cycles look correct.

## Deferred

- Neighbor browse / WebSocket status push
- Real Matter (Google Home) and real USB HID
- Custom phone/server apps (use the API)

## License

GPL-3.0 with additional terms — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
