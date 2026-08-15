#include "pc_state.h"
#include "configuration.h"
#include "nvs_prefs.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pc_state";

static volatile pc_power_state_t s_state = PC_STATE_UNKNOWN;
static volatile int s_gpio = CONFIG_PC_STATE_GPIO;

const char *pc_state_to_string(pc_power_state_t state)
{
    switch (state) {
    case PC_STATE_OFF:
        return "OFF";
    case PC_STATE_ON:
        return "ON";
    case PC_STATE_SLEEP:
        return "SLEEP";
    default:
        return "UNKNOWN";
    }
}

static esp_err_t configure_sense_gpio(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static void pc_state_task(void *arg)
{
    (void)arg;
    /* V1: steady ON/OFF from open-collector PC817.
     * Blink/sleep detection can refine this without changing the API. */
    int last = -1;
    int stable_count = 0;

    while (true) {
        const int gpio = s_gpio;
        const int level = gpio_get_level(gpio);
        const bool led_active = (level == PC_STATE_ACTIVE_LEVEL);

        if (level == last) {
            if (stable_count < 1000) {
                stable_count++;
            }
        } else {
            last = level;
            stable_count = 0;
        }

        /* ~1.5s steady before committing ON/OFF (avoids chatter). */
        const int needed = (int)(PC_STATE_STEADY_MS / PC_STATE_SAMPLE_MS);
        if (stable_count >= needed) {
            s_state = led_active ? PC_STATE_ON : PC_STATE_OFF;
        }

        vTaskDelay(pdMS_TO_TICKS(PC_STATE_SAMPLE_MS));
    }
}

esp_err_t pc_state_init(void)
{
    const waketype_settings_t *s = nvs_prefs_get();
    if (s && waketype_gpio_allowed(s->pc_state_gpio)) {
        s_gpio = s->pc_state_gpio;
    }

    esp_err_t err = configure_sense_gpio(s_gpio);
    if (err != ESP_OK) {
        return err;
    }

    s_state = PC_STATE_UNKNOWN;
    BaseType_t ok = xTaskCreate(pc_state_task, "pc_state", 3072, NULL, 5, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "PC state sense on GPIO%d (pull-up, active=%d)",
             s_gpio, PC_STATE_ACTIVE_LEVEL);
    return ESP_OK;
}

esp_err_t pc_state_set_gpio(uint8_t gpio)
{
    if (!waketype_gpio_allowed(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = configure_sense_gpio(gpio);
    if (err != ESP_OK) {
        return err;
    }
    s_gpio = gpio;
    s_state = PC_STATE_UNKNOWN;
    ESP_LOGI(TAG, "PC state sense now GPIO%u", (unsigned)gpio);
    return ESP_OK;
}

int pc_state_gpio(void)
{
    return s_gpio;
}

int pc_state_raw_level(void)
{
    return gpio_get_level(s_gpio);
}

pc_power_state_t pc_state_get(void)
{
    return s_state;
}
