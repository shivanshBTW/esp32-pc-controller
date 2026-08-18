#include "mdns_service.h"
#include "configuration.h"
#include "matter_controller.h"
#include "nvs_prefs.h"
#include "pc_controller.h"
#include "pc_state.h"

#include "esp_log.h"
#include "mdns.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mdns";
static bool s_started;

static void sanitize_hostname(const char *in, char *out, size_t out_len)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-') {
            out[j++] = (char)tolower(c);
        }
    }
    out[j] = '\0';
    if (j == 0) {
        strncpy(out, HOSTNAME_DEFAULT, out_len - 1);
    }
}

esp_err_t mdns_service_start(void)
{
    if (s_started) {
        mdns_service_update_txt();
        return ESP_OK;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }

    char host[33];
    sanitize_hostname(nvs_prefs_get()->hostname, host, sizeof(host));
    mdns_hostname_set(host);
    mdns_instance_name_set(PRODUCT_NAME);

    err = mdns_service_add(PRODUCT_NAME, MDNS_SERVICE_TYPE, MDNS_SERVICE_PROTO, API_HTTP_PORT, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mdns_service_add: %s", esp_err_to_name(err));
    }

    s_started = true;
    mdns_service_update_txt();
    ESP_LOGI(TAG, "mDNS http://%s.local (%s%s)", host, MDNS_SERVICE_TYPE, MDNS_SERVICE_PROTO);
    return ESP_OK;
}

void mdns_service_update_txt(void)
{
    if (!s_started) {
        return;
    }
    char ver[24];
    char api[8];
    char state[16];
    snprintf(ver, sizeof(ver), "%s", FIRMWARE_VERSION);
    snprintf(api, sizeof(api), "%d", CONFIG_SCHEMA_VERSION);
    snprintf(state, sizeof(state), "%s", pc_state_to_string(pc_controller_get_pc_state()));

    mdns_txt_item_t txt[] = {
        {.key = "version", .value = ver},
        {.key = "api", .value = api},
        {.key = "product", .value = PRODUCT_NAME},
        {.key = "pc_state", .value = state},
        {.key = "matter", .value = matter_controller_is_commissioned() ? "yes" : "pairing"},
    };
    mdns_service_txt_set(MDNS_SERVICE_TYPE, MDNS_SERVICE_PROTO, txt, sizeof(txt) / sizeof(txt[0]));
}
