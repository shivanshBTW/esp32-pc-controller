#include "pc_controller.h"
#include "configuration.h"
#include "diagnostics.h"
#include "relay_controller.h"

#include "esp_log.h"

static const char *TAG = "pc";

esp_err_t pc_controller_init(void)
{
    ESP_LOGI(TAG, "PC controller ready (power=%ums reset=%ums long_default=%ums max=%ums)",
             (unsigned)POWER_PRESS_MS, (unsigned)RESET_PRESS_MS,
             (unsigned)DEFAULT_LONG_PRESS_MS, (unsigned)MAX_RELAY_HOLD_MS);
    return ESP_OK;
}

esp_err_t pc_controller_power_press(void)
{
    diagnostics_note_command("power_press");
    ESP_LOGI(TAG, "powerPress()");
    return relay_controller_pulse(RELAY_CHANNEL_POWER, POWER_PRESS_MS);
}

esp_err_t pc_controller_power_hold(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        duration_ms = DEFAULT_LONG_PRESS_MS;
    }
    if (duration_ms > MAX_RELAY_HOLD_MS) {
        ESP_LOGW(TAG, "powerHold requested %u ms — clamping to %u ms",
                 (unsigned)duration_ms, (unsigned)MAX_RELAY_HOLD_MS);
        duration_ms = MAX_RELAY_HOLD_MS;
    }

    diagnostics_note_command("power_hold");
    ESP_LOGI(TAG, "powerHold(%u)", (unsigned)duration_ms);
    return relay_controller_pulse(RELAY_CHANNEL_POWER, duration_ms);
}

esp_err_t pc_controller_reset_press(void)
{
    diagnostics_note_command("reset_press");
    ESP_LOGW(TAG, "resetPress() — destructive");
    return relay_controller_pulse(RELAY_CHANNEL_RESET, RESET_PRESS_MS);
}

esp_err_t pc_controller_release_all(void)
{
    diagnostics_note_command("release_all");
    return relay_controller_release_all();
}

pc_power_state_t pc_controller_get_pc_state(void)
{
    return pc_state_get();
}
