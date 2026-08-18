#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Matter over Wi-Fi (Google Home / Apple Home).
 * Only the safe short power pulse is exposed: On/Off/Toggle.
 * Long-hold / reset / HID stay on the authenticated local API.
 */
esp_err_t matter_controller_start(void);

bool matter_controller_is_ready(void);
bool matter_controller_is_commissioned(void);

/** Manual pairing code (e.g. 34970112332). Empty until Matter starts. */
const char *matter_manual_pairing_code(void);

/** QR payload starting with MT: — paste/scan in Google Home. */
const char *matter_qr_payload(void);

/** Re-open BLE + DNS-SD pairing for 15 minutes. */
esp_err_t matter_controller_open_commissioning_window(void);

#ifdef __cplusplus
}
#endif
