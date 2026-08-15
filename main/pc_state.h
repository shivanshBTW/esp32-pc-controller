#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_STATE_OFF = 0,
    PC_STATE_ON,
    PC_STATE_SLEEP,   /* blinking power LED — refined later */
    PC_STATE_UNKNOWN,
} pc_power_state_t;

esp_err_t pc_state_init(void);

/** Live-switch PC817 sense GPIO. */
esp_err_t pc_state_set_gpio(uint8_t gpio);

int pc_state_gpio(void);

/** Live 0/1 on the sense GPIO (1 = pulled up / LED off for default wiring). */
int pc_state_raw_level(void);

/** Latest interpreted state from PC817 feedback (not inferred from relay presses). */
pc_power_state_t pc_state_get(void);

const char *pc_state_to_string(pc_power_state_t state);

#ifdef __cplusplus
}
#endif
