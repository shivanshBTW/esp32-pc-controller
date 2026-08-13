#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_scan_ap_t;

esp_err_t network_init(void);

bool network_is_sta_connected(void);
bool network_is_ap_active(void);
/** True when SoftAP setup portal should accept unauthenticated Wi‑Fi config. */
bool network_is_setup_mode(void);

const char *network_ip_string(void);
const char *network_mode_string(void);
/** Configured / currently associated STA SSID (empty if none). */
const char *network_sta_ssid(void);
int network_rssi(void);

typedef struct {
    char ip[16];
    char gateway[16];
    char netmask[16];
    char dns1[16];
    char dns2[16];
    bool use_static;
} network_ip_info_t;

/** Live STA/AP interface addresses (DHCP lease or static). */
void network_get_ip_info(network_ip_info_t *out);

esp_err_t network_save_and_connect(const char *ssid, const char *password);
esp_err_t network_apply_ip_settings(void);

/** Caller frees with free(). */
esp_err_t network_scan(wifi_scan_ap_t **out_list, size_t *out_count);

#ifdef __cplusplus
}
#endif
