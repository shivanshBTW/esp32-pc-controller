#include "configuration.h"
#include "diagnostics.h"
#include "hid_controller.h"
#include "local_api.h"
#include "matter_controller.h"
#include "network.h"
#include "nvs_prefs.h"
#include "pc_controller.h"
#include "pc_state.h"
#include "relay_controller.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "%s %s starting", PRODUCT_NAME, FIRMWARE_VERSION);

    /*
     * Boot order (safety first):
     * 1-4 relays safe/off
     * 5 prefs / PC-state
     * 6 USB HID
     * 7-8 networking (STA or SoftAP)
     * 9 local API + web UI
     * 10 Matter stub
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

    ESP_ERROR_CHECK(nvs_prefs_init());
    ESP_ERROR_CHECK(nvs_prefs_ensure_api_token());
    ESP_ERROR_CHECK(diagnostics_init());
    ESP_ERROR_CHECK(pc_state_init());
    ESP_ERROR_CHECK(pc_controller_init());
    ESP_ERROR_CHECK(hid_controller_init());

    ESP_ERROR_CHECK(network_init());
    local_api_start();

    matter_controller_start();

    ESP_LOGI(TAG, "Boot complete. Relays released. Physical PC buttons unaffected.");
}
