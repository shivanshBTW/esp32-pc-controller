#include "local_api.h"
#include "configuration.h"
#include "diagnostics.h"
#include "hid_controller.h"
#include "network.h"
#include "pc_controller.h"
#include "relay_controller.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

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
    const char *prefix = "Bearer ";
    if (strncmp(hdr, prefix, strlen(prefix)) != 0) {
        return false;
    }
    return constant_time_eq(hdr + strlen(prefix), API_TOKEN);
}

static esp_err_t reject_unauthorized(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
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

static esp_err_t handle_status(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", diagnostics_firmware_version());
    cJSON_AddNumberToObject(root, "uptime_sec", diagnostics_uptime_sec());
    cJSON_AddStringToObject(root, "last_command", diagnostics_last_command());
    cJSON_AddStringToObject(root, "pc_state",
                            pc_state_to_string(pc_controller_get_pc_state()));
    cJSON_AddBoolToObject(root, "wifi_connected", network_is_connected());
    cJSON_AddStringToObject(root, "ip", network_ip_string());
    cJSON_AddNumberToObject(root, "rssi", network_rssi());
    cJSON_AddBoolToObject(root, "relay_busy", relay_controller_is_busy());
    cJSON_AddBoolToObject(root, "power_relay_active", relay_controller_power_active());
    cJSON_AddBoolToObject(root, "reset_relay_active", relay_controller_reset_active());
    cJSON_AddBoolToObject(root, "hid_ready", hid_controller_is_ready());
    cJSON_AddStringToObject(root, "matter", "stub");
    return send_json(req, root);
}

static esp_err_t handle_power(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    return send_result(req, pc_controller_power_press());
}

static esp_err_t handle_power_hold(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }

    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    uint32_t duration = DEFAULT_LONG_PRESS_MS;
    if (received > 0) {
        buf[received] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            const cJSON *ms = cJSON_GetObjectItem(json, "duration_ms");
            if (cJSON_IsNumber(ms) && ms->valuedouble > 0) {
                duration = (uint32_t)ms->valuedouble;
            }
            cJSON_Delete(json);
        }
    }
    return send_result(req, pc_controller_power_hold(duration));
}

static esp_err_t handle_reset(httpd_req_t *req)
{
    if (!authorize(req)) {
        return reject_unauthorized(req);
    }
    return send_result(req, pc_controller_reset_press());
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

    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"missing body\"}");
    }
    buf[received] = '\0';

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

esp_err_t local_api_start(void)
{
    if (!network_is_connected()) {
        ESP_LOGW(TAG, "Skipping API — no Wi-Fi");
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = API_HTTP_PORT;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = handle_status},
        {.uri = "/api/pc/power", .method = HTTP_POST, .handler = handle_power},
        {.uri = "/api/pc/power/hold", .method = HTTP_POST, .handler = handle_power_hold},
        {.uri = "/api/pc/reset", .method = HTTP_POST, .handler = handle_reset},
        {.uri = "/api/pc/release", .method = HTTP_POST, .handler = handle_release},
        {.uri = "/api/hid/key", .method = HTTP_POST, .handler = handle_hid_key},
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    ESP_LOGI(TAG, "Local API listening on http://%s/api/*", network_ip_string());
    return ESP_OK;
}
