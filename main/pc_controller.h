#pragma once

#include "esp_err.h"
#include "pc_state.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_CMD_SOURCE_LOCAL_API = 0,
    PC_CMD_SOURCE_MATTER,
    PC_CMD_SOURCE_INTERNAL,
} pc_cmd_source_t;

esp_err_t pc_controller_init(void);

esp_err_t pc_controller_power_press(pc_cmd_source_t source);
esp_err_t pc_controller_power_hold(uint32_t duration_ms, pc_cmd_source_t source);
esp_err_t pc_controller_reset_press(pc_cmd_source_t source);
esp_err_t pc_controller_release_all(void);

bool pc_controller_is_local_lock(void);
pc_power_state_t pc_controller_get_pc_state(void);

#ifdef __cplusplus
}
#endif
