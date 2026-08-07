#pragma once

#include "esp_err.h"
#include "pc_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Central hardware authority. Matter, HTTP, and tests MUST call these —
 * never relay GPIOs directly.
 */
esp_err_t pc_controller_init(void);

esp_err_t pc_controller_power_press(void);
esp_err_t pc_controller_power_hold(uint32_t duration_ms);
esp_err_t pc_controller_reset_press(void);
esp_err_t pc_controller_release_all(void);

pc_power_state_t pc_controller_get_pc_state(void);

#ifdef __cplusplus
}
#endif
