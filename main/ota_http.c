#include "ota_http.h"
#include "nvs_prefs.h"
#include "pc_controller.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ota";

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

static esp_err_t handle_ota(httpd_req_t *req)
{
    if (!authorize(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
    }

    /* Safety: never leave relays held across reboot/update. */
    pc_controller_release_all();

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"error\":\"no ota partition\"}");
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"error\":\"ota begin failed\"}");
    }

    char buf[1024];
    int remaining = req->content_len;
    ESP_LOGI(TAG, "OTA upload %d bytes -> %s", remaining, part->label);

    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            esp_ota_abort(handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"error\":\"recv failed\"}");
        }
        err = esp_ota_write(handle, buf, received);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"error\":\"ota write failed\"}");
        }
        remaining -= received;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"error\":\"ota end failed\"}");
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"error\":\"set boot failed\"}");
    }

    pc_controller_release_all();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");
    ESP_LOGI(TAG, "OTA success — rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_http_register(httpd_handle_t server)
{
    httpd_uri_t uri = {
        .uri = "/api/v1/ota",
        .method = HTTP_POST,
        .handler = handle_ota,
    };
    return httpd_register_uri_handler(server, &uri);
}
