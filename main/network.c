#include "network.h"
#include "captive_dns.h"
#include "configuration.h"
#include "mdns_service.h"
#include "nvs_prefs.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "net";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_prepared;
static bool s_sta_connected;
static bool s_ap_active;
static bool s_want_sta;
static int s_retry;
static char s_ip[16] = "0.0.0.0";
static int s_rssi;

static void apply_hostname(void)
{
    if (!s_sta_netif) {
        return;
    }
    const char *host = nvs_prefs_get()->hostname;
    if (!host || host[0] == '\0') {
        host = HOSTNAME_DEFAULT;
    }
    esp_netif_set_hostname(s_sta_netif, host);
    ESP_LOGI(TAG, "DHCP hostname: %s", host);
}

static bool parse_ipv4(const char *text, esp_ip4_addr_t *out)
{
    if (!text || text[0] == '\0' || !out) {
        return false;
    }
    /* Reject common UI mistakes: spaces, CIDR suffixes, incomplete values */
    for (const char *p = text; *p; p++) {
        if (*p == ' ' || *p == '/') {
            return false;
        }
    }
    return esp_netif_str_to_ip4(text, out) == ESP_OK;
}

static esp_err_t start_dhcp_client(void)
{
    esp_err_t err = esp_netif_dhcpc_start(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return err;
    }
    ESP_LOGI(TAG, "STA IP mode: DHCP");
    return ESP_OK;
}

static void disable_broken_static_config(const char *reason)
{
    waketype_settings_t *s = nvs_prefs_mutable();
    ESP_LOGE(TAG, "Static IP invalid (%s) ip='%s' gw='%s' mask='%s' — falling back to DHCP",
             reason, s->wifi_ip, s->wifi_gateway, s->wifi_netmask);
    s->wifi_use_static = false;
    nvs_prefs_save();
}

static esp_err_t apply_static_or_dhcp(void)
{
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }
    const waketype_settings_t *s = nvs_prefs_get();
    if (!s->wifi_use_static) {
        return start_dhcp_client();
    }

    esp_netif_ip_info_t ip_info = {0};
    const char *mask = s->wifi_netmask[0] ? s->wifi_netmask : "255.255.255.0";
    if (!parse_ipv4(s->wifi_ip, &ip_info.ip) ||
        !parse_ipv4(s->wifi_gateway, &ip_info.gw) ||
        !parse_ipv4(mask, &ip_info.netmask)) {
        disable_broken_static_config("bad address format");
        return start_dhcp_client();
    }

    esp_err_t stop = esp_netif_dhcpc_stop(s_sta_netif);
    if (stop != ESP_OK && stop != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcpc_stop: %s (continuing)", esp_err_to_name(stop));
    }

    esp_err_t err = esp_netif_set_ip_info(s_sta_netif, &ip_info);
    if (err != ESP_OK) {
        disable_broken_static_config(esp_err_to_name(err));
        return start_dhcp_client();
    }

    const char *dns1 = s->wifi_dns1[0] ? s->wifi_dns1 : s->wifi_gateway;
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    if (parse_ipv4(dns1, &dns.ip.u_addr.ip4)) {
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (s->wifi_dns2[0] && parse_ipv4(s->wifi_dns2, &dns.ip.u_addr.ip4)) {
        esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
    }

    ESP_LOGI(TAG, "STA IP mode: static %s gw %s", s->wifi_ip, s->wifi_gateway);
    return ESP_OK;
}

static esp_err_t start_ap(void)
{
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, AP_SSID_DEFAULT, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen(AP_SSID_DEFAULT);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    {
        esp_err_t start = esp_wifi_start();
        if (start != ESP_OK && start != ESP_ERR_INVALID_STATE) {
            return start;
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    s_ap_active = true;
    s_want_sta = false;
    s_sta_connected = false;
    strncpy(s_ip, "192.168.4.1", sizeof(s_ip) - 1);

    esp_netif_ip_info_t ip_info;
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        captive_dns_start(ip_info.ip.addr);
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip_info.ip));
    } else {
        captive_dns_start(esp_netif_htonl(ESP_IP4TOADDR(192, 168, 4, 1)));
    }

    ESP_LOGW(TAG, "SoftAP '%s' at http://%s — configure Wi‑Fi", AP_SSID_DEFAULT, s_ip);
    return ESP_OK;
}

static esp_err_t start_sta(const char *ssid, const char *password)
{
    captive_dns_stop();
    s_ap_active = false;
    s_want_sta = true;
    s_retry = 0;
    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);

    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta.sta.password, password, sizeof(sta.sta.password) - 1);
    }
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    apply_hostname();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    {
        esp_err_t start = esp_wifi_start();
        if (start != ESP_OK && start != ESP_ERR_INVALID_STATE) {
            return start;
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    /* Never abort boot on bad static IP — fall back to DHCP inside helper. */
    if (apply_static_or_dhcp() != ESP_OK) {
        ESP_LOGW(TAG, "IP config failed; continuing with driver defaults");
    }
    ESP_LOGI(TAG, "Connecting STA to '%s'...", ssid);
    return ESP_OK;
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        if (s_want_sta) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        if (!s_want_sta) {
            return;
        }
        if (s_retry < WIFI_STA_FAIL_BEFORE_AP) {
            s_retry++;
            ESP_LOGW(TAG, "STA reconnect %d/%d", s_retry, WIFI_STA_FAIL_BEFORE_AP);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "STA failed — opening setup SoftAP (relays stay released)");
            start_ap();
        }
    } else if (id == WIFI_EVENT_AP_START) {
        s_ap_active = true;
    } else if (id == WIFI_EVENT_AP_STOP) {
        s_ap_active = false;
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    ip_event_got_ip_t *event = data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_retry = 0;
    s_sta_connected = true;
    s_ap_active = false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s_rssi = ap.rssi;
    }
    xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "STA connected, IP %s", s_ip);
    mdns_service_start();
}

esp_err_t network_prepare(void)
{
    if (s_prepared) {
        return ESP_OK;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) {
        return loop;
    }

    s_sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    s_ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (!s_sta_netif || !s_ap_netif) {
        return ESP_FAIL;
    }
    apply_hostname();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip, NULL));
    s_prepared = true;
    return ESP_OK;
}

/*
 * CHIP calls esp_wifi_init() again from esp_matter::start(). If the driver is
 * already up, that returns ESP_ERR_INVALID_STATE and Matter never starts.
 */
esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config);

esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config)
{
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGI(TAG, "Wi-Fi already initialized — Matter will reuse it");
        return ESP_OK;
    }
    return __real_esp_wifi_init(config);
}

esp_err_t network_init(void)
{
    esp_err_t err = network_prepare();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    const waketype_settings_t *s = nvs_prefs_get();
    if (s->wifi_ssid[0] != '\0') {
        err = start_sta(s->wifi_ssid, s->wifi_password);
        if (err != ESP_OK) {
            return start_ap();
        }
        EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(25000));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "STA timeout — SoftAP setup");
            return start_ap();
        }
        return ESP_OK;
    }
    return start_ap();
}

bool network_is_sta_connected(void)
{
    return s_sta_connected;
}

bool network_is_ap_active(void)
{
    return s_ap_active;
}

bool network_is_setup_mode(void)
{
    return s_ap_active && !s_sta_connected;
}

const char *network_ip_string(void)
{
    return s_ip;
}

const char *network_mode_string(void)
{
    if (s_sta_connected) {
        return "sta";
    }
    if (s_ap_active) {
        return "ap";
    }
    return "down";
}

const char *network_sta_ssid(void)
{
    /* Prefer live association; fall back to saved prefs. */
    if (s_sta_connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK && ap.ssid[0] != '\0') {
            static char live[33];
            strncpy(live, (char *)ap.ssid, sizeof(live) - 1);
            live[sizeof(live) - 1] = '\0';
            return live;
        }
    }
    return nvs_prefs_get()->wifi_ssid;
}

void network_get_ip_info(network_ip_info_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->use_static = nvs_prefs_get()->wifi_use_static;
    strncpy(out->ip, s_ip, sizeof(out->ip) - 1);

    esp_netif_t *netif = s_sta_connected ? s_sta_netif : (s_ap_active ? s_ap_netif : NULL);
    if (!netif) {
        return;
    }

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
        snprintf(out->ip, sizeof(out->ip), IPSTR, IP2STR(&info.ip));
        snprintf(out->gateway, sizeof(out->gateway), IPSTR, IP2STR(&info.gw));
        snprintf(out->netmask, sizeof(out->netmask), IPSTR, IP2STR(&info.netmask));
    }

    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4) {
        snprintf(out->dns1, sizeof(out->dns1), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    }
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4 && dns.ip.u_addr.ip4.addr != 0) {
        snprintf(out->dns2, sizeof(out->dns2), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    }
}

int network_rssi(void)
{
    if (!s_sta_connected) {
        return 0;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        s_rssi = ap.rssi;
    }
    return s_rssi;
}

bool network_ipv4_ok(const char *text)
{
    esp_ip4_addr_t tmp;
    return parse_ipv4(text, &tmp);
}

esp_err_t network_apply_ip_settings(void)
{
    apply_hostname();
    return apply_static_or_dhcp();
}

esp_err_t network_save_and_connect(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    waketype_settings_t *s = nvs_prefs_mutable();
    memset(s->wifi_ssid, 0, sizeof(s->wifi_ssid));
    memset(s->wifi_password, 0, sizeof(s->wifi_password));
    strncpy(s->wifi_ssid, ssid, sizeof(s->wifi_ssid) - 1);
    if (password) {
        strncpy(s->wifi_password, password, sizeof(s->wifi_password) - 1);
    }
    esp_err_t err = nvs_prefs_save();
    if (err != ESP_OK) {
        return err;
    }

    s_retry = 0;
    s_want_sta = true;
    captive_dns_stop();

    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta.sta.password, password, sizeof(sta.sta.password) - 1);
    }
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    apply_hostname();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    apply_static_or_dhcp();
    s_ap_active = false;
    esp_wifi_connect();
    ESP_LOGI(TAG, "Saved Wi‑Fi '%s' — connecting", ssid);
    return ESP_OK;
}

esp_err_t network_scan(wifi_scan_ap_t **out_list, size_t *out_count)
{
    if (!out_list || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_list = NULL;
    *out_count = 0;

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }

    /* Drop any previous scan results; allow scan while STA is associated. */
    esp_wifi_scan_stop();
    wifi_scan_config_t scan = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err == ESP_ERR_WIFI_STATE) {
        vTaskDelay(pdMS_TO_TICKS(200));
        err = esp_wifi_scan_start(&scan, true);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi‑Fi scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num == 0) {
        return ESP_OK;
    }
    if (ap_num > 40) {
        ap_num = 40;
    }

    wifi_ap_record_t *records = calloc(ap_num, sizeof(wifi_ap_record_t));
    wifi_scan_ap_t *list = calloc(ap_num, sizeof(wifi_scan_ap_t));
    if (!records || !list) {
        free(records);
        free(list);
        return ESP_ERR_NO_MEM;
    }

    uint16_t count = ap_num;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        free(records);
        free(list);
        return err;
    }

    for (uint16_t i = 0; i < count; i++) {
        strncpy(list[i].ssid, (char *)records[i].ssid, sizeof(list[i].ssid) - 1);
        list[i].rssi = records[i].rssi;
        list[i].authmode = records[i].authmode;
    }
    free(records);
    *out_list = list;
    *out_count = count;
    return ESP_OK;
}
