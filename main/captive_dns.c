#include "captive_dns.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "captive_dns";

static int s_sock = -1;
static volatile bool s_running;
static TaskHandle_t s_task;
static uint32_t s_ap_ip_be;

static void captive_dns_task(void *arg)
{
    (void)arg;
    uint8_t buf[512];

    while (s_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 12) {
            continue;
        }

        uint8_t response[512];
        if ((size_t)n + 16 > sizeof(response)) {
            continue;
        }
        memcpy(response, buf, (size_t)n);
        response[2] = 0x81;
        response[3] = 0x80;
        response[6] = 0x00;
        response[7] = 0x01;

        size_t pos = (size_t)n;
        response[pos++] = 0xC0;
        response[pos++] = 0x0C;
        response[pos++] = 0x00;
        response[pos++] = 0x01;
        response[pos++] = 0x00;
        response[pos++] = 0x01;
        response[pos++] = 0x00;
        response[pos++] = 0x00;
        response[pos++] = 0x00;
        response[pos++] = 0x3C;
        response[pos++] = 0x00;
        response[pos++] = 0x04;
        memcpy(response + pos, &s_ap_ip_be, 4);
        pos += 4;

        sendto(s_sock, response, pos, 0, (struct sockaddr *)&from, from_len);
    }
    vTaskDelete(NULL);
}

esp_err_t captive_dns_start(uint32_t ap_ip_be)
{
    captive_dns_stop();
    s_ap_ip_be = ap_ip_be;

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        return ESP_FAIL;
    }
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    s_running = true;
    if (xTaskCreate(captive_dns_task, "captive_dns", 3072, NULL, 5, &s_task) != pdPASS) {
        captive_dns_stop();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Captive DNS started");
    return ESP_OK;
}

void captive_dns_stop(void)
{
    s_running = false;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    s_task = NULL;
}
