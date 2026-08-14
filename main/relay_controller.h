#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RELAY_CHANNEL_POWER = 0,
    RELAY_CHANNEL_RESET = 1,
} relay_channel_t;

/**
 * Boot-critical: drive both relay GPIOs to inactive ASAP.
 * Call after nvs_prefs_init() so saved pin choices are used.
 */
esp_err_t relay_controller_early_init(void);

/** Full init: mutex, watchdog timer, verify released. */
esp_err_t relay_controller_init(void);

/** Activate a channel for duration_ms, then release. Blocks the calling task. */
esp_err_t relay_controller_pulse(relay_channel_t channel, uint32_t duration_ms);

/** Immediately force both relays OFF. Cancels any in-progress pulse. */
esp_err_t relay_controller_release_all(void);

/** Live-switch relay GPIOs (forces both OFF first). */
esp_err_t relay_controller_set_pins(uint8_t power_gpio, uint8_t reset_gpio);

int relay_controller_power_gpio(void);
int relay_controller_reset_gpio(void);

bool relay_controller_is_busy(void);
bool relay_controller_power_active(void);
bool relay_controller_reset_active(void);

#ifdef __cplusplus
}
#endif
