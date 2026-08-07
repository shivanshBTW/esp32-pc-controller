#include "matter_controller.h"
#include "pc_controller.h"

#include "esp_log.h"

static const char *TAG = "matter";

/*
 * Full Espressif Matter integration comes after relay + API are proven.
 * This stub documents the required behavior and the single allowed call path.
 *
 * Google Home "turn on/off PC" → pc_controller_power_press() only.
 * Never: powerHold, reset, raw relay, or indefinite ON.
 */

esp_err_t matter_controller_start(void)
{
    /* Future: Matter On/Off cluster → pc_controller_power_press() only. */
    ESP_LOGW(TAG, "Matter stub — Google Home will only use short powerPress()");
    return ESP_OK;
}
