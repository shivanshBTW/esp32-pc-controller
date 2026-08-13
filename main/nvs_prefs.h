#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    bool wifi_use_static;
    char wifi_ip[16];
    char wifi_gateway[16];
    char wifi_netmask[16];
    char wifi_dns1[16];
    char wifi_dns2[16];
    char hostname[33];
    char api_token[65];
    bool local_lock;
    bool local_lock_blocks_api;
    uint32_t power_press_ms;
    uint32_t reset_press_ms;
    uint32_t default_long_press_ms;
} waketype_settings_t;

esp_err_t nvs_prefs_init(void);
const waketype_settings_t *nvs_prefs_get(void);
waketype_settings_t *nvs_prefs_mutable(void);
esp_err_t nvs_prefs_save(void);
esp_err_t nvs_prefs_ensure_api_token(void);

#ifdef __cplusplus
}
#endif
