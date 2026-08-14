#include "relay_controller.h"
#include "configuration.h"
#include "nvs_prefs.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "relay";

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_watchdog;
static volatile bool s_cancel;
static volatile bool s_power_active;
static volatile bool s_reset_active;
static volatile bool s_busy;

static gpio_num_t s_power_gpio = (gpio_num_t)CONFIG_PC_POWER_RELAY_GPIO;
static gpio_num_t s_reset_gpio = (gpio_num_t)CONFIG_PC_RESET_RELAY_GPIO;

static void relay_write(relay_channel_t channel, bool active)
{
    const gpio_num_t pin =
        (channel == RELAY_CHANNEL_POWER) ? s_power_gpio : s_reset_gpio;
    const int level = active ? relay_active_level() : relay_inactive_level();
    gpio_set_level(pin, level);

    if (channel == RELAY_CHANNEL_POWER) {
        s_power_active = active;
    } else {
        s_reset_active = active;
    }
}

static void relay_force_inactive_both(void)
{
    gpio_set_level(s_power_gpio, relay_inactive_level());
    gpio_set_level(s_reset_gpio, relay_inactive_level());
    s_power_active = false;
    s_reset_active = false;
}

static void watchdog_cb(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "WATCHDOG: forcing relays OFF (exceeded safety window)");
    s_cancel = true;
    relay_force_inactive_both();
    s_busy = false;
}

static esp_err_t configure_relay_gpio(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Inactive first — never leave floating into an active-low coil driver. */
    return gpio_set_level(pin, relay_inactive_level());
}

static void force_inactive_pin(gpio_num_t pin)
{
    if (!waketype_gpio_allowed((int)pin)) {
        return;
    }
    configure_relay_gpio(pin);
}

esp_err_t relay_controller_early_init(void)
{
    const waketype_settings_t *s = nvs_prefs_get();
    const int pwr = s ? s->power_relay_gpio : CONFIG_PC_POWER_RELAY_GPIO;
    const int rst = s ? s->reset_relay_gpio : CONFIG_PC_RESET_RELAY_GPIO;
    const int st = s ? s->pc_state_gpio : CONFIG_PC_STATE_GPIO;

    if (waketype_gpio_trio_ok(pwr, rst, st)) {
        s_power_gpio = (gpio_num_t)pwr;
        s_reset_gpio = (gpio_num_t)rst;
    } else {
        s_power_gpio = (gpio_num_t)CONFIG_PC_POWER_RELAY_GPIO;
        s_reset_gpio = (gpio_num_t)CONFIG_PC_RESET_RELAY_GPIO;
    }

    ESP_LOGI(TAG, "Early safe state: POWER=GPIO%d RESET=GPIO%d active_low=%d",
             (int)s_power_gpio, (int)s_reset_gpio, (int)RELAY_ACTIVE_LOW);

    /* Also park compile-time defaults if the user moved away from them. */
    if ((int)s_power_gpio != CONFIG_PC_POWER_RELAY_GPIO &&
        (int)s_reset_gpio != CONFIG_PC_POWER_RELAY_GPIO) {
        force_inactive_pin((gpio_num_t)CONFIG_PC_POWER_RELAY_GPIO);
    }
    if ((int)s_power_gpio != CONFIG_PC_RESET_RELAY_GPIO &&
        (int)s_reset_gpio != CONFIG_PC_RESET_RELAY_GPIO) {
        force_inactive_pin((gpio_num_t)CONFIG_PC_RESET_RELAY_GPIO);
    }

    esp_err_t err = configure_relay_gpio(s_power_gpio);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_relay_gpio(s_reset_gpio);
    if (err != ESP_OK) {
        return err;
    }
    relay_force_inactive_both();
    return ESP_OK;
}

esp_err_t relay_controller_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_watchdog == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &watchdog_cb,
            .name = "relay_wd",
        };
        esp_err_t err = esp_timer_create(&args, &s_watchdog);
        if (err != ESP_OK) {
            return err;
        }
    }

    s_cancel = false;
    s_busy = false;
    relay_force_inactive_both();
    ESP_LOGI(TAG, "Relay controller ready (max hold %u ms, watchdog %u ms)",
             (unsigned)MAX_RELAY_HOLD_MS, (unsigned)RELAY_WATCHDOG_MS);
    return ESP_OK;
}

esp_err_t relay_controller_set_pins(uint8_t power_gpio, uint8_t reset_gpio)
{
    if (!waketype_gpio_allowed(power_gpio) || !waketype_gpio_allowed(reset_gpio) ||
        power_gpio == reset_gpio) {
        return ESP_ERR_INVALID_ARG;
    }

    relay_controller_release_all();

    const gpio_num_t old_pwr = s_power_gpio;
    const gpio_num_t old_rst = s_reset_gpio;

    esp_err_t err = configure_relay_gpio((gpio_num_t)power_gpio);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_relay_gpio((gpio_num_t)reset_gpio);
    if (err != ESP_OK) {
        configure_relay_gpio(old_pwr);
        configure_relay_gpio(old_rst);
        return err;
    }

    s_power_gpio = (gpio_num_t)power_gpio;
    s_reset_gpio = (gpio_num_t)reset_gpio;
    relay_force_inactive_both();

    if (old_pwr != s_power_gpio && old_pwr != s_reset_gpio) {
        force_inactive_pin(old_pwr);
    }
    if (old_rst != s_power_gpio && old_rst != s_reset_gpio && old_rst != old_pwr) {
        force_inactive_pin(old_rst);
    }

    ESP_LOGI(TAG, "Relay pins now POWER=GPIO%u RESET=GPIO%u",
             (unsigned)power_gpio, (unsigned)reset_gpio);
    return ESP_OK;
}

int relay_controller_power_gpio(void)
{
    return (int)s_power_gpio;
}

int relay_controller_reset_gpio(void)
{
    return (int)s_reset_gpio;
}

esp_err_t relay_controller_release_all(void)
{
    s_cancel = true;
    if (s_watchdog) {
        esp_timer_stop(s_watchdog);
    }
    relay_force_inactive_both();
    s_busy = false;
    ESP_LOGW(TAG, "Emergency release: both relays OFF");
    return ESP_OK;
}

bool relay_controller_is_busy(void)
{
    return s_busy;
}

bool relay_controller_power_active(void)
{
    return s_power_active;
}

bool relay_controller_reset_active(void)
{
    return s_reset_active;
}

esp_err_t relay_controller_pulse(relay_channel_t channel, uint32_t duration_ms)
{
    if (duration_ms == 0 || duration_ms > MAX_RELAY_HOLD_MS) {
        ESP_LOGE(TAG, "Rejected pulse duration %u ms (max %u)",
                 (unsigned)duration_ms, (unsigned)MAX_RELAY_HOLD_MS);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Relay busy — request rejected");
        return ESP_ERR_INVALID_STATE;
    }

    /* Mutual exclusion: never energize POWER and RESET together. */
    if (channel == RELAY_CHANNEL_POWER && s_reset_active) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "POWER rejected — RESET active");
        return ESP_ERR_INVALID_STATE;
    }
    if (channel == RELAY_CHANNEL_RESET && s_power_active) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "RESET rejected — POWER active");
        return ESP_ERR_INVALID_STATE;
    }

    s_cancel = false;
    s_busy = true;

    esp_err_t err = esp_timer_start_once(s_watchdog, (uint64_t)RELAY_WATCHDOG_MS * 1000ULL);
    if (err != ESP_OK) {
        s_busy = false;
        xSemaphoreGive(s_lock);
        return err;
    }

    ESP_LOGI(TAG, "Pulse %s for %u ms",
             channel == RELAY_CHANNEL_POWER ? "POWER" : "RESET",
             (unsigned)duration_ms);

    relay_write(channel, true);

    const TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);
    while (xTaskGetTickCount() < end) {
        if (s_cancel) {
            ESP_LOGW(TAG, "Pulse cancelled");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    relay_write(channel, false);
    esp_timer_stop(s_watchdog);
    s_busy = false;
    s_cancel = false;

    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Pulse complete — relays released");
    return ESP_OK;
}
