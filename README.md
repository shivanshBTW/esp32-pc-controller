# WakeType

ESP32-S3 appliance firmware for remote PC power, reset, and USB HID keyboard control.

WakeType is **not** an operating system and is **not** LumosOS. It lives inside a desktop PC and safely pulses motherboard POWER/RESET switch headers, reports power state, and (later) types via native USB.

## Hardware

- ESP32-S3-WROOM-1-N16R8 (dual USB-C DevKit)
- 2-channel 5V optocoupler relay module (POWER + RESET, COM/NO in parallel with case buttons)
- PC817 optocoupler for POWER LED sense
- Always-powered PC USB port for the ESP32

Default GPIOs (change anytime in **Settings → GPIO pins**, or `idf.py menuconfig` → WakeType for compile-time defaults):

| Function | Default GPIO |
|----------|--------------|
| POWER relay | 4 |
| RESET relay | 5 |
| PC state (PC817) | 6 |

Relays default to **active-low**. Example alternate wiring: 13 / 14 / 6.

## Safety

- Relays forced OFF at boot (before Wi‑Fi/HTTP)
- No permanent relay-on API
- Firmware owns pulse timing (max 8 s) + watchdog
- POWER/RESET mutual exclusion
- Emergency `POST /api/v1/pc/release`
- OTA forces relays OFF before flashing
- Local lock can block API remote commands; physical case buttons still work

## Device key (`secrets.env`)

Like a JS `.env`: a local file you edit, **not committed**.

```bash
cp secrets.env.example secrets.env
# edit WAKETYPE_DEVICE_KEY=your-password
```

Then rebuild/flash. That password is the WakeType web/API **device key**.

| Variable | Meaning |
|----------|---------|
| `WAKETYPE_DEVICE_KEY` | Password you choose |
| `WAKETYPE_FORCE_DEVICE_KEY=1` | Overwrite key already stored on the board (use once after changing password) |

On `http://waketype.local/settings`, paste the **same** password under Device key → Save.

## First boot (Wi‑Fi)

1. Put your password in `secrets.env`, then flash.
2. Phone/laptop joins Wi‑Fi hotspot **`WakeType-Setup`**.
3. Open `http://192.168.4.1/settings`.
4. Scan → pick home Wi‑Fi → Connect.
5. On LAN open `http://waketype.local/settings`, paste the same device key, Save.

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

## Home Assistant / Google Assistant

YAML in [`homeassistant/`](homeassistant/) talks to this API over the LAN.

On the Ubuntu host (the folder that already has `compose.yaml` and `config/`):

```bash
# from this repo, on the Mac
./homeassistant/sync_secrets.sh

# on the HA machine, from the homeassistant/ directory that contains compose.yaml
mkdir -p config/packages
# copy packages/waketype.yaml, google_assistant.yaml, and secrets.yaml into config/
```

In `config/configuration.yaml`, add `packages` under the existing `homeassistant:` block (do not add a second `homeassistant:` key):

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Replace the existing `google_assistant:` mapping with:

```yaml
google_assistant: !include google_assistant.yaml
```

Merge `waketype_bearer` into `config/secrets.yaml` (do not commit it). Restart HA, then say **“Hey Google, sync my devices.”**

Dashboard buttons: **Press PC power**, **Reset the PC**, **Force shut down**, plus **Bedroom PC** (on/off). Google: those actions as scenes behind **PC controls**, **PC power** as an open/closed tile.

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
- Real USB HID
- Custom phone/server apps (use the API)

## License

GPL-3.0 with additional terms — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
