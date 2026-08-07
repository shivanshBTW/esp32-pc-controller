#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t network_init(void);

bool network_is_connected(void);
const char *network_ip_string(void);
int network_rssi(void);

#ifdef __cplusplus
}
#endif
