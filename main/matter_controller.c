#include "matter_controller.h"
#include "pc_controller.h"

#include "esp_log.h"

static const char *TAG = "matter";

esp_err_t matter_controller_start(void)
{
    /* Future: Matter On/Off → pc_controller_power_press(PC_CMD_SOURCE_MATTER) only. */
    ESP_LOGW(TAG, "Matter stub — Google Home will only use short powerPress()");
    return ESP_OK;
}
