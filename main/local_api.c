#include "local_api.h"
#include "configuration.h"
#include "diagnostics.h"
#include "hid_controller.h"
#include "matter_controller.h"
#include "mdns_service.h"
#include "network.h"
#include "nvs_prefs.h"
#include "ota_http.h"
#include "pc_controller.h"
#include "pc_state.h"
#include "relay_controller.h"
#include "web_ui.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "api";
static httpd_handle_t s_server;

static bool constant_time_eq(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t n = la > lb ? la : lb;
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = i < la ? (unsigned char)a[i] : 0;
        unsigned char cb = i < lb ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

static bool authorize(httpd_req_t *req)
{
    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    if (strncmp(hdr, "Bearer ", 7) != 0) {
        return false;
    }
    return constant_time_eq(hdr + 7, nvs_prefs_get()->api_token);
}

static bool authorize_or_setup(httpd_req_t *req)
{
    if (network_is_setup_mode()) {
        return true;
    }
    return authorize(req);
}

static esp_err_t reject_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req,
        "{\"error\":\"unauthorized\","
        "\"hint\":\"This browser is missing the device key. Open Settings and paste it, "
        "or join Wi-Fi WakeType-Setup and open http://192.168.4.1 to recover it.\"}");
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static esp_err_t send_result(httpd_req_t *req, esp_err_t err)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
    }
    return send_json(req, root);
}

static int recv_body(httpd_req_t *req, char *buf, size_t buflen)
{
    int total = 0;
    int remaining = req->content_len;
    if (remaining <= 0 || remaining >= (int)buflen) {
        remaining = (int)buflen - 1;
    }
    while (total < remaining) {
        int r = httpd_req_recv(req, buf + total, remaining - total);
        if (r <= 0) {
            break;
        }
        total += r;
    }
    buf[total] = '\0';
    return total;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    const bool setup = network_is_setup_mode();
    if (!setup && !authorize(req)) {
        return reject_unauthorized(req);
    }

    const waketype_settings_t *s = nvs_prefs_get();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "product", PRODUCT_NAME);
    cJSON_AddStringToObject(root, "firmware", diagnostics_firmware_version());
    cJSON_AddNumberToObject(root, "schema", CONFIG_SCHEMA_VERSION);
    cJSON_AddNumberToObject(root, "uptime_sec", diagnostics_uptime_sec());
    cJSON_AddStringToObject(root, "last_command", diagnostics_last_command());
    cJSON_AddStringToObject(root, "pc_state",
                            pc_state_to_string(pc_controller_get_pc_state()));
    network_ip_info_t ipinfo;
    network_get_ip_info(&ipinfo);

    cJSON_AddStringToObject(root, "wifi_mode", network_mode_string());
    cJSON_AddBoolToObject(root, "wifi_connected", network_is_sta_connected());
    cJSON_AddBoolToObject(root, "setup_mode", setup);
    cJSON_AddStringToObject(root, "wifi_ssid", network_sta_ssid());
    cJSON_AddBoolToObject(root, "wifi_password_set", s->wifi_password[0] != '\0');
    cJSON_AddStringToObject(root, "ip", ipinfo.ip[0] ? ipinfo.ip : network_ip_string());
    cJSON_AddStringToObject(root, "gateway", ipinfo.gateway);
    cJSON_AddStringToObject(root, "netmask", ipinfo.netmask);
    cJSON_AddStringToObject(root, "dns1", ipinfo.dns1);
    cJSON_AddStringToObject(root, "dns2", ipinfo.dns2);
    cJSON_AddBoolToObject(root, "wifi_use_static", ipinfo.use_static);
    cJSON_AddStringToObject(root, "hostname", s->hostname);
    cJSON_AddNumberToObject(root, "rssi", network_rssi());
    cJSON_AddBoolToObject(root, "relay_busy", relay_controller_is_busy());
    cJSON_AddBoolToObject(root, "power_relay_active", relay_controller_power_active());
    cJSON_AddBoolToObject(root, "reset_relay_active", relay_controller_reset_active());
    cJSON_AddNumberToObject(root, "power_relay_gpio", relay_controller_power_gpio());
    cJSON_AddNumberToObject(root, "reset_relay_gpio", relay_controller_reset_gpio());
    cJSON_AddNumberToObject(root, "pc_state_gpio", pc_state_gpio());
    cJSON_AddNumberToObject(root, "pc_state_raw", pc_state_raw_level());
    cJSON_AddBoolToObject(root, "local_lock", pc_controller_is_local_lock());
    cJSON_AddBoolToObject(root, "hid_ready", hid_controller_is_ready());
    cJSON_AddBoolToObject(root, "matter_ready", matter_controller_is_ready());
    cJSON_AddBoolToObject(root, "matter_commissioned", matter_controller_is_commissioned());
    cJSON_AddStringToObject(root, "matter", matter_controller_is_commissioned() ? "commissioned" : "pairing");
    if (matter_controller_last_error()[0]) {
        cJSON_AddStringToObject(root, "matter_error", matter_controller_last_error());
    }
    if (setup || authorize(req)) {
        cJSON_AddStringToObject(root, "matter_pairing_code", matter_manual_pairing_code());
        cJSON_AddStringToObject(root, "matter_qr", matter_qr_payload());
    }
    cJSON_AddNumberToObject(root, "heap_free", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "heap_min", (double)esp_get_minimum_free_heap_size());
    /* Setup hotspot: always include key so the browser can remember it.
     * When already authorized: include it so Settings can show/copy it. */
    if (setup || authorize(req)) {
        cJSON_AddStringToObject(root, "device_key", s->api_token);
        cJSON_AddStringToObject(root, "api_token", s->api_token); /* alias */
    }
    mdns_service_update_txt();
    return send_json(req, root);
}

static esp_err_t handle_wifi_scan(httpd_req_t *req)
{
    if (!authorize_or_setup(req)) {
        return reject_unauthorized(req);
    }
    wifi_scan_ap_t *list = NULL;
    size_t count = 0;
    esp_err_t err = network_scan(&list, &count);
    if (err != ESP_OK) {
        return send_result(req, err);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *aps = cJSON_AddArrayToObject(root, "aps");
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", list[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", list[i].rssi);
        cJSON_AddNumberToObject(item, "authmode", list[i].authmode);
        cJSON_AddItemToArray(aps, item);
    }
    free(list);
    return send_json(req, root);
}

static esp_err_t handle_wifi_connect(httpd_req_t *req)
{
    if (!authorize_or_setup(req)) {
        return reject_unauthorized(req);
    }
    char buf[256];
    if (recv_body(req, buf, sizeof(buf)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"missing body\"}");
    }
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"invalid json\"}");
    }
    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    const cJSON *pass = cJSON_GetObjectItem(json, "password");
    const char *ssid_s = cJSON_IsString(ssid) ? ssid->valuestring : NULL;
    const char *pass_s = cJSON_IsString(pass) ? pass->valuestring : "";
    esp_err_t err = network_save_and_connect(ssid_s, pass_s);
    cJSON_Delete(json);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", err == ESP_OK);
    cJSON_AddStringToObject(root, "error", esp_err_to_name(err));
    if (err == ESP_OK) {
        cJSON_AddStringToObject(root, "device_key", nvs_prefs_get()->api_token);
        cJSON_AddStringToObject(root, "api_token", nvs_prefs_get()->api_token);
        cJSON_AddStringToObject(root, "next_url_hint",
                                "After the board joins Wi-Fi, open http://waketype.local/settings?key=YOUR_DEVICE_KEY");
    } else {
        httpd_resp_set_status(req, "409 Conflict");
    }
    return send_json(req, root);
}

static esp_err_t handle_settings_get(httpd_req_t *req)
{
    /* Setup SoftAP: allow read for captive UI. STA: require bearer token. */
    if (!network_is_setup_mode() && !authorize(req)) {
        return reject_unauthorized(req);
    }

    const waketype_settings_t *s = nvs_prefs_get();
    network_ip_info_t live;
    network_get_ip_info(&live);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "hostname", s->hostname);
    cJSON_AddStringToObject(root, "wifi_ssid", network_sta_ssid());
    cJSON_AddBoolToObject(root, "wifi_password_set", s->wifi_password[0] != '\0');
    cJSON_AddBoolToObject(root, "wifi_connected", network_is_sta_connected());
    cJSON_AddStringToObject(root, "wifi_mode", network_mode_string());
    /* Live lease / interface addresses (what the device is using now) */
    cJSON_AddStringToObject(root, "current_ip", live.ip);
    cJSON_AddStringToObject(root, "current_gateway", live.gateway);
    cJSON_AddStringToObject(root, "current_netmask", live.netmask);
    cJSON_AddStringToObject(root, "current_dns1", live.dns1);
    cJSON_AddStringToObject(root, "current_dns2", live.dns2);
    /* Saved static-IP config (may be empty when using DHCP) */
    cJSON_AddBoolToObject(root, "wifi_use_static", s->wifi_use_static);
    cJSON_AddStringToObject(root, "wifi_ip", s->wifi_ip);
    cJSON_AddStringToObject(root, "wifi_gateway", s->wifi_gateway);
    cJSON_AddStringToObject(root, "wifi_netmask", s->wifi_netmask);
    cJSON_AddStringToObject(root, "wifi_dns1", s->wifi_dns1);
    cJSON_AddStringToObject(root, "wifi_dns2", s->wifi_dns2);
    cJSON_AddBoolToObject(root, "local_lock", s->local_lock);
    cJSON_AddBoolToObject(root, "local_lock_blocks_api", s->local_lock_blocks_api);
    cJSON_AddNumberToObject(root, "power_press_ms", s->power_press_ms);
    cJSON_AddNumberToObject(root, "reset_press_ms", s->reset_press_ms);
    cJSON_AddNumberToObject(root, "default_long_press_ms", s->default_long_press_ms);
    cJSON_AddNumberToObject(root, "max_relay_hold_ms", MAX_RELAY_HOLD_MS);
    cJSON_AddNumberToObject(root, "power_relay_gpio", s->power_relay_gpio);
    cJSON_AddNumberToObject(root, "reset_relay_gpio", s->reset_relay_gpio);
    cJSON_AddNumberToObject(root, "pc_state_gpio", s->pc_state_gpio);
    cJSON_AddBoolToObject(root, "matter_ready", matter_controller_is_ready());
    cJSON_AddBoolToObject(root, "matter_commissioned", matter_controller_is_commissioned());
    if (matter_controller_last_error()[0]) {
        cJSON_AddStringToObject(root, "matter_error", matter_controller_last_error());
    }
    if (network_is_setup_mode() || authorize(req)) {
        cJSON_AddStringToObject(root, "device_key", s->api_token);
        cJSON_AddStringToObject(root, "api_token", s->api_token);
        cJSON_AddStringToObject(root, "matter_pairing_code", matter_manual_pairing_code());
        cJSON_AddStringToObject(root, "matter_qr", matter_qr_payload());
    }
    return send_json(req, root);
}

static void copy_json_str(const cJSON *obj, const char *key, char *dst, size_t dst_len)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, dst_len - 1);
        dst[dst_len - 1] = '\0';
    }
}

static esp_err_t handle_settings_post(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    char buf[1024];
    if (recv_body(req, buf, sizeof(buf)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"missing body\"}");
    }
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"invalid json\"}");
    }

    waketype_settings_t *s = nvs_prefs_mutable();
    copy_json_str(json, "hostname", s->hostname, sizeof(s->hostname));
    copy_json_str(json, "wifi_ip", s->wifi_ip, sizeof(s->wifi_ip));
    copy_json_str(json, "wifi_gateway", s->wifi_gateway, sizeof(s->wifi_gateway));
    copy_json_str(json, "wifi_netmask", s->wifi_netmask, sizeof(s->wifi_netmask));
    copy_json_str(json, "wifi_dns1", s->wifi_dns1, sizeof(s->wifi_dns1));
    copy_json_str(json, "wifi_dns2", s->wifi_dns2, sizeof(s->wifi_dns2));
    copy_json_str(json, "api_token", s->api_token, sizeof(s->api_token));

    const cJSON *b;
    b = cJSON_GetObjectItem(json, "wifi_use_static");
    if (cJSON_IsBool(b)) {
        s->wifi_use_static = cJSON_IsTrue(b);
    }

    if (s->wifi_use_static) {
        const char *mask = s->wifi_netmask[0] ? s->wifi_netmask : "255.255.255.0";
        if (!network_ipv4_ok(s->wifi_ip) || !network_ipv4_ok(s->wifi_gateway) ||
            !network_ipv4_ok(mask) ||
            (s->wifi_dns1[0] && !network_ipv4_ok(s->wifi_dns1)) ||
            (s->wifi_dns2[0] && !network_ipv4_ok(s->wifi_dns2))) {
            cJSON_Delete(json);
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req,
                "{\"error\":\"invalid_static_ip\","
                "\"hint\":\"Use full IPv4 like 192.168.1.50, gateway 192.168.1.1, netmask 255.255.255.0\"}");
        }
        if (s->wifi_netmask[0] == '\0') {
            strncpy(s->wifi_netmask, "255.255.255.0", sizeof(s->wifi_netmask) - 1);
        }
    }
    b = cJSON_GetObjectItem(json, "local_lock");
    if (cJSON_IsBool(b)) {
        s->local_lock = cJSON_IsTrue(b);
    }
    b = cJSON_GetObjectItem(json, "local_lock_blocks_api");
    if (cJSON_IsBool(b)) {
        s->local_lock_blocks_api = cJSON_IsTrue(b);
    }

    const cJSON *n;
    n = cJSON_GetObjectItem(json, "power_press_ms");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) {
        s->power_press_ms = (uint32_t)n->valuedouble;
    }
    n = cJSON_GetObjectItem(json, "reset_press_ms");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) {
        s->reset_press_ms = (uint32_t)n->valuedouble;
    }
    n = cJSON_GetObjectItem(json, "default_long_press_ms");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) {
        uint32_t v = (uint32_t)n->valuedouble;
        if (v > MAX_RELAY_HOLD_MS) {
            v = MAX_RELAY_HOLD_MS;
        }
        s->default_long_press_ms = v;
    }

    uint8_t pwr_g = s->power_relay_gpio;
    uint8_t rst_g = s->reset_relay_gpio;
    uint8_t st_g = s->pc_state_gpio;
    bool gpio_touch = false;
    n = cJSON_GetObjectItem(json, "power_relay_gpio");
    if (cJSON_IsNumber(n)) {
        pwr_g = (uint8_t)n->valuedouble;
        gpio_touch = true;
    }
    n = cJSON_GetObjectItem(json, "reset_relay_gpio");
    if (cJSON_IsNumber(n)) {
        rst_g = (uint8_t)n->valuedouble;
        gpio_touch = true;
    }
    n = cJSON_GetObjectItem(json, "pc_state_gpio");
    if (cJSON_IsNumber(n)) {
        st_g = (uint8_t)n->valuedouble;
        gpio_touch = true;
    }
    if (gpio_touch) {
        if (!waketype_gpio_trio_ok(pwr_g, rst_g, st_g)) {
            cJSON_Delete(json);
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req,
                "{\"error\":\"invalid_gpio\","
                "\"hint\":\"Use three different safe GPIOs (e.g. 4/5/6 or 13/14/6). "
                "Avoid USB 19/20, flash 33-37, and strapping pins.\"}");
        }
        s->power_relay_gpio = pwr_g;
        s->reset_relay_gpio = rst_g;
        s->pc_state_gpio = st_g;
    }
    cJSON_Delete(json);

    esp_err_t err = nvs_prefs_save();
    if (err == ESP_OK) {
        network_apply_ip_settings();
        mdns_service_start();
        if (gpio_touch) {
            err = relay_controller_set_pins(s->power_relay_gpio, s->reset_relay_gpio);
            if (err == ESP_OK) {
                err = pc_state_set_gpio(s->pc_state_gpio);
            }
        }
    }
    return send_result(req, err);
}

static esp_err_t handle_config_get(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    const waketype_settings_t *s = nvs_prefs_get();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "product", PRODUCT_NAME);
    cJSON_AddNumberToObject(root, "schema", CONFIG_SCHEMA_VERSION);
    cJSON_AddStringToObject(root, "hostname", s->hostname);
    cJSON_AddStringToObject(root, "wifi_ssid", s->wifi_ssid);
    cJSON_AddBoolToObject(root, "wifi_use_static", s->wifi_use_static);
    cJSON_AddStringToObject(root, "wifi_ip", s->wifi_ip);
    cJSON_AddStringToObject(root, "wifi_gateway", s->wifi_gateway);
    cJSON_AddStringToObject(root, "wifi_netmask", s->wifi_netmask);
    cJSON_AddStringToObject(root, "wifi_dns1", s->wifi_dns1);
    cJSON_AddStringToObject(root, "wifi_dns2", s->wifi_dns2);
    cJSON_AddBoolToObject(root, "local_lock", s->local_lock);
    cJSON_AddBoolToObject(root, "local_lock_blocks_api", s->local_lock_blocks_api);
    cJSON_AddNumberToObject(root, "power_press_ms", s->power_press_ms);
    cJSON_AddNumberToObject(root, "reset_press_ms", s->reset_press_ms);
    cJSON_AddNumberToObject(root, "default_long_press_ms", s->default_long_press_ms);
    cJSON_AddNumberToObject(root, "power_relay_gpio", s->power_relay_gpio);
    cJSON_AddNumberToObject(root, "reset_relay_gpio", s->reset_relay_gpio);
    cJSON_AddNumberToObject(root, "pc_state_gpio", s->pc_state_gpio);
    /* secrets omitted by default */
    cJSON_AddBoolToObject(root, "wifi_password_set", s->wifi_password[0] != '\0');
    cJSON_AddBoolToObject(root, "api_token_set", s->api_token[0] != '\0');
    return send_json(req, root);
}

static esp_err_t handle_config_post(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    char buf[2048];
    if (recv_body(req, buf, sizeof(buf)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"missing body\"}");
    }
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"invalid json\"}");
    }

    pc_controller_release_all();

    waketype_settings_t *s = nvs_prefs_mutable();
    copy_json_str(json, "hostname", s->hostname, sizeof(s->hostname));
    copy_json_str(json, "wifi_ssid", s->wifi_ssid, sizeof(s->wifi_ssid));
    copy_json_str(json, "wifi_ip", s->wifi_ip, sizeof(s->wifi_ip));
    copy_json_str(json, "wifi_gateway", s->wifi_gateway, sizeof(s->wifi_gateway));
    copy_json_str(json, "wifi_netmask", s->wifi_netmask, sizeof(s->wifi_netmask));
    copy_json_str(json, "wifi_dns1", s->wifi_dns1, sizeof(s->wifi_dns1));
    copy_json_str(json, "wifi_dns2", s->wifi_dns2, sizeof(s->wifi_dns2));

    const cJSON *inc = cJSON_GetObjectItem(json, "include_secrets");
    if (cJSON_IsTrue(inc)) {
        copy_json_str(json, "wifi_password", s->wifi_password, sizeof(s->wifi_password));
        copy_json_str(json, "api_token", s->api_token, sizeof(s->api_token));
    }

    const cJSON *clear_static = cJSON_GetObjectItem(json, "clear_static_ip");
    if (!cJSON_IsFalse(clear_static)) {
        /* default: clear static IP on import (cloning) */
        s->wifi_use_static = false;
        s->wifi_ip[0] = '\0';
        s->wifi_gateway[0] = '\0';
        s->wifi_dns1[0] = '\0';
        s->wifi_dns2[0] = '\0';
    } else {
        const cJSON *b = cJSON_GetObjectItem(json, "wifi_use_static");
        if (cJSON_IsBool(b)) {
            s->wifi_use_static = cJSON_IsTrue(b);
        }
    }

    const cJSON *b = cJSON_GetObjectItem(json, "local_lock");
    if (cJSON_IsBool(b)) {
        s->local_lock = cJSON_IsTrue(b);
    }
    b = cJSON_GetObjectItem(json, "local_lock_blocks_api");
    if (cJSON_IsBool(b)) {
        s->local_lock_blocks_api = cJSON_IsTrue(b);
    }

    const cJSON *n = cJSON_GetObjectItem(json, "power_press_ms");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) {
        s->power_press_ms = (uint32_t)n->valuedouble;
    }
    n = cJSON_GetObjectItem(json, "reset_press_ms");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) {
        s->reset_press_ms = (uint32_t)n->valuedouble;
    }
    n = cJSON_GetObjectItem(json, "default_long_press_ms");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) {
        uint32_t v = (uint32_t)n->valuedouble;
        if (v > MAX_RELAY_HOLD_MS) {
            v = MAX_RELAY_HOLD_MS;
        }
        s->default_long_press_ms = v;
    }

    uint8_t pwr_g = s->power_relay_gpio;
    uint8_t rst_g = s->reset_relay_gpio;
    uint8_t st_g = s->pc_state_gpio;
    n = cJSON_GetObjectItem(json, "power_relay_gpio");
    if (cJSON_IsNumber(n)) {
        pwr_g = (uint8_t)n->valuedouble;
    }
    n = cJSON_GetObjectItem(json, "reset_relay_gpio");
    if (cJSON_IsNumber(n)) {
        rst_g = (uint8_t)n->valuedouble;
    }
    n = cJSON_GetObjectItem(json, "pc_state_gpio");
    if (cJSON_IsNumber(n)) {
        st_g = (uint8_t)n->valuedouble;
    }
    if (waketype_gpio_trio_ok(pwr_g, rst_g, st_g)) {
        s->power_relay_gpio = pwr_g;
        s->reset_relay_gpio = rst_g;
        s->pc_state_gpio = st_g;
    }
    cJSON_Delete(json);

    esp_err_t err = nvs_prefs_save();
    if (err == ESP_OK) {
        network_apply_ip_settings();
        relay_controller_set_pins(s->power_relay_gpio, s->reset_relay_gpio);
        pc_state_set_gpio(s->pc_state_gpio);
    }
    return send_result(req, err);
}

static esp_err_t handle_power(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    return send_result(req, pc_controller_power_press(PC_CMD_SOURCE_LOCAL_API));
}

static esp_err_t handle_power_hold(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    char buf[128];
    uint32_t duration = 0;
    if (recv_body(req, buf, sizeof(buf)) > 0) {
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            const cJSON *ms = cJSON_GetObjectItem(json, "duration_ms");
            if (cJSON_IsNumber(ms) && ms->valuedouble > 0) {
                duration = (uint32_t)ms->valuedouble;
            }
            cJSON_Delete(json);
        }
    }
    return send_result(req, pc_controller_power_hold(duration, PC_CMD_SOURCE_LOCAL_API));
}

static esp_err_t handle_reset(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    return send_result(req, pc_controller_reset_press(PC_CMD_SOURCE_LOCAL_API));
}

static esp_err_t handle_release(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    return send_result(req, pc_controller_release_all());
}

static hid_command_t parse_hid_key(const char *name)
{
    if (!name) {
        return (hid_command_t)-1;
    }
    if (strcmp(name, "enter") == 0) return HID_KEY_ENTER;
    if (strcmp(name, "escape") == 0) return HID_KEY_ESCAPE;
    if (strcmp(name, "delete") == 0) return HID_KEY_DELETE;
    if (strcmp(name, "backspace") == 0) return HID_KEY_BACKSPACE;
    if (strcmp(name, "tab") == 0) return HID_KEY_TAB;
    if (strcmp(name, "up") == 0) return HID_KEY_UP;
    if (strcmp(name, "down") == 0) return HID_KEY_DOWN;
    if (strcmp(name, "left") == 0) return HID_KEY_LEFT;
    if (strcmp(name, "right") == 0) return HID_KEY_RIGHT;
    if (strcmp(name, "f1") == 0) return HID_KEY_F1;
    if (strcmp(name, "f2") == 0) return HID_KEY_F2;
    if (strcmp(name, "f3") == 0) return HID_KEY_F3;
    if (strcmp(name, "f4") == 0) return HID_KEY_F4;
    if (strcmp(name, "f5") == 0) return HID_KEY_F5;
    if (strcmp(name, "f6") == 0) return HID_KEY_F6;
    if (strcmp(name, "f7") == 0) return HID_KEY_F7;
    if (strcmp(name, "f8") == 0) return HID_KEY_F8;
    if (strcmp(name, "f9") == 0) return HID_KEY_F9;
    if (strcmp(name, "f10") == 0) return HID_KEY_F10;
    if (strcmp(name, "f11") == 0) return HID_KEY_F11;
    if (strcmp(name, "f12") == 0) return HID_KEY_F12;
    if (strcmp(name, "ctrl_alt_delete") == 0) return HID_COMBO_CTRL_ALT_DELETE;
    return (hid_command_t)-1;
}

static esp_err_t handle_hid_key(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    if (pc_controller_is_local_lock() && nvs_prefs_get()->local_lock_blocks_api) {
        return send_result(req, ESP_ERR_INVALID_STATE);
    }

    char buf[128];
    if (recv_body(req, buf, sizeof(buf)) <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"missing body\"}");
    }
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"invalid json\"}");
    }
    const cJSON *key = cJSON_GetObjectItem(json, "key");
    hid_command_t cmd = parse_hid_key(cJSON_IsString(key) ? key->valuestring : NULL);
    cJSON_Delete(json);
    if ((int)cmd < 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"unknown key\"}");
    }
    return send_result(req, hid_controller_send(cmd));
}

static esp_err_t handle_matter_commission(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    if (!matter_controller_is_ready()) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        const char *why = matter_controller_last_error();
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "error", why[0] ? why : "ESP_ERR_INVALID_STATE");
        cJSON_AddStringToObject(root, "hint", "Matter is not running. Update firmware, then try pairing again.");
        return send_json(req, root);
    }
    return send_result(req, matter_controller_open_commissioning_window());
}

esp_err_t local_api_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = API_HTTP_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 24;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = handle_status},
        {.uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = handle_wifi_scan},
        {.uri = "/api/v1/wifi/connect", .method = HTTP_POST, .handler = handle_wifi_connect},
        {.uri = "/api/v1/settings", .method = HTTP_GET, .handler = handle_settings_get},
        {.uri = "/api/v1/settings", .method = HTTP_POST, .handler = handle_settings_post},
        {.uri = "/api/v1/config", .method = HTTP_GET, .handler = handle_config_get},
        {.uri = "/api/v1/config", .method = HTTP_POST, .handler = handle_config_post},
        {.uri = "/api/v1/pc/power", .method = HTTP_POST, .handler = handle_power},
        {.uri = "/api/v1/pc/power/hold", .method = HTTP_POST, .handler = handle_power_hold},
        {.uri = "/api/v1/pc/reset", .method = HTTP_POST, .handler = handle_reset},
        {.uri = "/api/v1/pc/release", .method = HTTP_POST, .handler = handle_release},
        {.uri = "/api/v1/hid/key", .method = HTTP_POST, .handler = handle_hid_key},
        {.uri = "/api/v1/matter/commission", .method = HTTP_POST, .handler = handle_matter_commission},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    web_ui_register(s_server);
    ota_http_register(s_server);

    ESP_LOGI(TAG, "HTTP on http://%s/ (mode=%s)", network_ip_string(), network_mode_string());
    return ESP_OK;
}
