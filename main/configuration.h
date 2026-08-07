#pragma once

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Firmware identity */
#define FIRMWARE_VERSION "0.1.0-dev"

/* GPIO map — provisional for dual USB-C ESP32-S3-WROOM-1-N16R8 DevKit.
 * Change via menuconfig (idf.py menuconfig) if your wiring differs. */
#define POWER_RELAY_GPIO          CONFIG_PC_POWER_RELAY_GPIO
#define RESET_RELAY_GPIO          CONFIG_PC_RESET_RELAY_GPIO
#define PC_STATE_GPIO             CONFIG_PC_STATE_GPIO

#define RELAY_ACTIVE_LOW          CONFIG_PC_RELAY_ACTIVE_LOW

#define POWER_PRESS_MS            CONFIG_PC_POWER_PRESS_MS
#define RESET_PRESS_MS            CONFIG_PC_RESET_PRESS_MS
#define DEFAULT_LONG_PRESS_MS     CONFIG_PC_DEFAULT_LONG_PRESS_MS
#define MAX_RELAY_HOLD_MS         CONFIG_PC_MAX_RELAY_HOLD_MS

/* Watchdog fires slightly above the absolute max hold. */
#define RELAY_WATCHDOG_MS         (MAX_RELAY_HOLD_MS + 500)

/* Wi-Fi / API — from menuconfig; empty SSID means "skip connect". */
#define WIFI_SSID                 CONFIG_PC_WIFI_SSID
#define WIFI_PASSWORD             CONFIG_PC_WIFI_PASSWORD
#define API_TOKEN                 CONFIG_PC_API_TOKEN
#define API_HTTP_PORT             80

/* PC817 open-collector: active LED pulls GPIO LOW when using INPUT_PULLUP. */
#define PC_STATE_ACTIVE_LEVEL     0

/* Sleep/blink detection thresholds (ms) — for later refinement. */
#define PC_STATE_SAMPLE_MS        50
#define PC_STATE_STEADY_MS        1500
#define PC_STATE_BLINK_MIN_MS     200
#define PC_STATE_BLINK_MAX_MS     2000

static inline int relay_active_level(void)
{
    return RELAY_ACTIVE_LOW ? 0 : 1;
}

static inline int relay_inactive_level(void)
{
    return RELAY_ACTIVE_LOW ? 1 : 0;
}

#ifdef __cplusplus
}
#endif
