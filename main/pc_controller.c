#include "pc_controller.h"
#include "configuration.h"
#include "diagnostics.h"
#include "nvs_prefs.h"
#include "relay_controller.h"

#include "esp_log.h"

static const char *TAG = "pc";

static bool command_blocked(pc_cmd_source_t source)
{
    const waketype_settings_t *s = nvs_prefs_get();
    if (!s->local_lock) {
        return false;
    }
    if (source == PC_CMD_SOURCE_INTERNAL) {
        return false;
    }
    if (source == PC_CMD_SOURCE_MATTER) {
        return true;
    }
    /* LOCAL_API */
    return s->local_lock_blocks_api;
}

esp_err_t pc_controller_init(void)
{
    const waketype_settings_t *s = nvs_prefs_get();
    ESP_LOGI(TAG, "PC controller ready (power=%ums reset=%ums long_default=%ums max=%ums lock=%d)",
             (unsigned)s->power_press_ms, (unsigned)s->reset_press_ms,
             (unsigned)s->default_long_press_ms, (unsigned)MAX_RELAY_HOLD_MS,
             (int)s->local_lock);
    return ESP_OK;
}

esp_err_t pc_controller_power_press(pc_cmd_source_t source)
{
    if (command_blocked(source)) {
        ESP_LOGW(TAG, "powerPress blocked by local lock");
        return ESP_ERR_INVALID_STATE;
    }
    diagnostics_note_command("power_press");
    ESP_LOGI(TAG, "powerPress()");
    return relay_controller_pulse(RELAY_CHANNEL_POWER, nvs_prefs_get()->power_press_ms);
}

esp_err_t pc_controller_power_hold(uint32_t duration_ms, pc_cmd_source_t source)
{
    if (command_blocked(source)) {
        ESP_LOGW(TAG, "powerHold blocked by local lock");
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms == 0) {
        duration_ms = nvs_prefs_get()->default_long_press_ms;
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

esp_err_t pc_controller_reset_press(pc_cmd_source_t source)
{
    if (command_blocked(source)) {
        ESP_LOGW(TAG, "resetPress blocked by local lock");
        return ESP_ERR_INVALID_STATE;
    }
    diagnostics_note_command("reset_press");
    ESP_LOGW(TAG, "resetPress() — destructive");
    return relay_controller_pulse(RELAY_CHANNEL_RESET, nvs_prefs_get()->reset_press_ms);
}

esp_err_t pc_controller_release_all(void)
{
    diagnostics_note_command("release_all");
    return relay_controller_release_all();
}

bool pc_controller_is_local_lock(void)
{
    return nvs_prefs_get()->local_lock;
}

pc_power_state_t pc_controller_get_pc_state(void)
{
    return pc_state_get();
}
