#include "configuration.h"
#include "diagnostics.h"
#include "hid_controller.h"
#include "local_api.h"
#include "matter_controller.h"
#include "network.h"
#include "pc_controller.h"
#include "pc_state.h"
#include "relay_controller.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 PC Controller %s starting", FIRMWARE_VERSION);

    /*
     * Boot order (safety first):
     * 1-4 relays safe/off
     * 5 PC-state input
     * 6 USB HID
     * 7-8 networking
     * 9 local API
     * 10 Matter
     */

    ESP_ERROR_CHECK(relay_controller_early_init());
    ESP_ERROR_CHECK(relay_controller_init());
    ESP_ERROR_CHECK(pc_controller_release_all());

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    ESP_ERROR_CHECK(diagnostics_init());
    ESP_ERROR_CHECK(pc_state_init());
    ESP_ERROR_CHECK(pc_controller_init());
    ESP_ERROR_CHECK(hid_controller_init());

    esp_err_t net = network_init();
    if (net == ESP_OK && network_is_connected()) {
        local_api_start();
    } else {
        ESP_LOGW(TAG, "Network unavailable — hardware controls remain safe/local");
    }

    matter_controller_start();

    ESP_LOGI(TAG, "Boot complete. Relays released. Physical PC buttons unaffected.");
}
