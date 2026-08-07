#include "diagnostics.h"
#include "configuration.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static char s_last_cmd[48] = "none";
static SemaphoreHandle_t s_lock;
static int64_t s_boot_us;

esp_err_t diagnostics_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_boot_us = esp_timer_get_time();
    strncpy(s_last_cmd, "boot", sizeof(s_last_cmd) - 1);
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

void diagnostics_note_command(const char *name)
{
    if (!name) {
        return;
    }
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_last_cmd, name, sizeof(s_last_cmd) - 1);
        s_last_cmd[sizeof(s_last_cmd) - 1] = '\0';
        xSemaphoreGive(s_lock);
    }
}

const char *diagnostics_last_command(void)
{
    return s_last_cmd;
}

uint32_t diagnostics_uptime_sec(void)
{
    return (uint32_t)((esp_timer_get_time() - s_boot_us) / 1000000LL);
}

const char *diagnostics_firmware_version(void)
{
    return FIRMWARE_VERSION;
}
