#include "hid_controller.h"

#include "esp_log.h"

#include <stdbool.h>

static const char *TAG = "hid";
static bool s_ready;

/*
 * Native USB HID (TinyUSB) will be wired here once the Serial/JTAG vs native
 * USB port roles are validated on hardware.
 *
 * Until then, commands are accepted and logged so the local API shape is stable.
 */

esp_err_t hid_controller_init(void)
{
    s_ready = false;
    ESP_LOGW(TAG, "USB HID stub active — connect native USB port for real keyboard later");
    return ESP_OK;
}

bool hid_controller_is_ready(void)
{
    return s_ready;
}

esp_err_t hid_controller_send(hid_command_t cmd)
{
    ESP_LOGI(TAG, "HID command %d (stub — not yet sent over USB)", (int)cmd);
    return s_ready ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}
