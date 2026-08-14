#include "nvs_prefs.h"
#include "configuration.h"

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "prefs";
static const char *kNs = "waketype";

static waketype_settings_t s_settings;
static bool s_ready;

static void set_defaults(void)
{
    memset(&s_settings, 0, sizeof(s_settings));
    strncpy(s_settings.hostname, HOSTNAME_DEFAULT, sizeof(s_settings.hostname) - 1);
    strncpy(s_settings.wifi_netmask, "255.255.255.0", sizeof(s_settings.wifi_netmask) - 1);
    s_settings.power_press_ms = POWER_PRESS_MS_DEFAULT;
    s_settings.reset_press_ms = RESET_PRESS_MS_DEFAULT;
    s_settings.default_long_press_ms = DEFAULT_LONG_PRESS_MS_DEFAULT;
    s_settings.local_lock = false;
    s_settings.local_lock_blocks_api = true;
    s_settings.power_relay_gpio = (uint8_t)POWER_RELAY_GPIO;
    s_settings.reset_relay_gpio = (uint8_t)RESET_RELAY_GPIO;
    s_settings.pc_state_gpio = (uint8_t)PC_STATE_GPIO;
}

static void get_str(nvs_handle_t h, const char *key, char *out, size_t out_len)
{
    size_t len = out_len;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) {
        /* keep existing / default */
    }
}

esp_err_t nvs_prefs_init(void)
{
    set_defaults();
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_ready = true;
        ESP_LOGI(TAG, "No saved prefs — using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    get_str(h, "wifi_ssid", s_settings.wifi_ssid, sizeof(s_settings.wifi_ssid));
    get_str(h, "wifi_pass", s_settings.wifi_password, sizeof(s_settings.wifi_password));
    get_str(h, "wifi_ip", s_settings.wifi_ip, sizeof(s_settings.wifi_ip));
    get_str(h, "wifi_gw", s_settings.wifi_gateway, sizeof(s_settings.wifi_gateway));
    get_str(h, "wifi_mask", s_settings.wifi_netmask, sizeof(s_settings.wifi_netmask));
    get_str(h, "wifi_dns1", s_settings.wifi_dns1, sizeof(s_settings.wifi_dns1));
    get_str(h, "wifi_dns2", s_settings.wifi_dns2, sizeof(s_settings.wifi_dns2));
    get_str(h, "hostname", s_settings.hostname, sizeof(s_settings.hostname));
    get_str(h, "api_token", s_settings.api_token, sizeof(s_settings.api_token));

    uint8_t u8 = 0;
    if (nvs_get_u8(h, "static_ip", &u8) == ESP_OK) {
        s_settings.wifi_use_static = u8 != 0;
    }
    if (nvs_get_u8(h, "local_lock", &u8) == ESP_OK) {
        s_settings.local_lock = u8 != 0;
    }
    if (nvs_get_u8(h, "lock_api", &u8) == ESP_OK) {
        s_settings.local_lock_blocks_api = u8 != 0;
    }

    uint32_t u32 = 0;
    if (nvs_get_u32(h, "pwr_ms", &u32) == ESP_OK && u32 > 0) {
        s_settings.power_press_ms = u32;
    }
    if (nvs_get_u32(h, "rst_ms", &u32) == ESP_OK && u32 > 0) {
        s_settings.reset_press_ms = u32;
    }
    if (nvs_get_u32(h, "long_ms", &u32) == ESP_OK && u32 > 0) {
        s_settings.default_long_press_ms = u32;
    }

    uint8_t g = 0;
    uint8_t pwr = s_settings.power_relay_gpio;
    uint8_t rst = s_settings.reset_relay_gpio;
    uint8_t st = s_settings.pc_state_gpio;
    if (nvs_get_u8(h, "gpio_pwr", &g) == ESP_OK) {
        pwr = g;
    }
    if (nvs_get_u8(h, "gpio_rst", &g) == ESP_OK) {
        rst = g;
    }
    if (nvs_get_u8(h, "gpio_st", &g) == ESP_OK) {
        st = g;
    }
    if (waketype_gpio_trio_ok(pwr, rst, st)) {
        s_settings.power_relay_gpio = pwr;
        s_settings.reset_relay_gpio = rst;
        s_settings.pc_state_gpio = st;
    } else {
        ESP_LOGW(TAG, "Ignoring invalid saved GPIOs %u/%u/%u — using defaults",
                 (unsigned)pwr, (unsigned)rst, (unsigned)st);
    }

    nvs_close(h);
    if (s_settings.hostname[0] == '\0') {
        strncpy(s_settings.hostname, HOSTNAME_DEFAULT, sizeof(s_settings.hostname) - 1);
    }
    s_ready = true;
    ESP_LOGI(TAG, "Prefs loaded (ssid='%s' hostname='%s' gpio pwr=%u rst=%u state=%u)",
             s_settings.wifi_ssid, s_settings.hostname,
             (unsigned)s_settings.power_relay_gpio,
             (unsigned)s_settings.reset_relay_gpio,
             (unsigned)s_settings.pc_state_gpio);
    return ESP_OK;
}

const waketype_settings_t *nvs_prefs_get(void)
{
    return &s_settings;
}

waketype_settings_t *nvs_prefs_mutable(void)
{
    return &s_settings;
}

esp_err_t nvs_prefs_save(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    nvs_set_str(h, "wifi_ssid", s_settings.wifi_ssid);
    nvs_set_str(h, "wifi_pass", s_settings.wifi_password);
    nvs_set_str(h, "wifi_ip", s_settings.wifi_ip);
    nvs_set_str(h, "wifi_gw", s_settings.wifi_gateway);
    nvs_set_str(h, "wifi_mask", s_settings.wifi_netmask);
    nvs_set_str(h, "wifi_dns1", s_settings.wifi_dns1);
    nvs_set_str(h, "wifi_dns2", s_settings.wifi_dns2);
    nvs_set_str(h, "hostname", s_settings.hostname);
    nvs_set_str(h, "api_token", s_settings.api_token);
    nvs_set_u8(h, "static_ip", s_settings.wifi_use_static ? 1 : 0);
    nvs_set_u8(h, "local_lock", s_settings.local_lock ? 1 : 0);
    nvs_set_u8(h, "lock_api", s_settings.local_lock_blocks_api ? 1 : 0);
    nvs_set_u32(h, "pwr_ms", s_settings.power_press_ms);
    nvs_set_u32(h, "rst_ms", s_settings.reset_press_ms);
    nvs_set_u32(h, "long_ms", s_settings.default_long_press_ms);
    nvs_set_u8(h, "gpio_pwr", s_settings.power_relay_gpio);
    nvs_set_u8(h, "gpio_rst", s_settings.reset_relay_gpio);
    nvs_set_u8(h, "gpio_st", s_settings.pc_state_gpio);

    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Prefs saved");
    return err;
}

esp_err_t nvs_prefs_ensure_api_token(void)
{
#ifdef WAKETYPE_DEVICE_KEY
    const char *from_secrets = WAKETYPE_DEVICE_KEY;
    const bool have_secrets = from_secrets[0] != '\0';
#else
    const char *from_secrets = "";
    const bool have_secrets = false;
#endif

#if defined(WAKETYPE_FORCE_DEVICE_KEY) && (WAKETYPE_FORCE_DEVICE_KEY)
    const bool force = true;
#else
    const bool force = false;
#endif

    if (have_secrets) {
        if (force || s_settings.api_token[0] == '\0' ||
            strcmp(s_settings.api_token, from_secrets) != 0) {
            memset(s_settings.api_token, 0, sizeof(s_settings.api_token));
            strncpy(s_settings.api_token, from_secrets, sizeof(s_settings.api_token) - 1);
            ESP_LOGW(TAG, "Device key set from secrets.env%s", force ? " (forced)" : "");
            return nvs_prefs_save();
        }
        return ESP_OK;
    }

    if (s_settings.api_token[0] != '\0') {
        return ESP_OK;
    }

    /* No secrets.env key — generate random once */
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    for (int i = 0; i < 16; i++) {
        sprintf(&s_settings.api_token[i * 2], "%02x", raw[i]);
    }
    s_settings.api_token[32] = '\0';
    ESP_LOGW(TAG, "Generated random device key (add secrets.env to choose your own)");
    return nvs_prefs_save();
}
