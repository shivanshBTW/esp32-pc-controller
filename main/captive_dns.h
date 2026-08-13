#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Wildcard DNS for SoftAP captive portal. ap_ip_be = AP IPv4 in network byte order. */
esp_err_t captive_dns_start(uint32_t ap_ip_be);
void captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
