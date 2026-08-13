#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mdns_service_start(void);
void mdns_service_update_txt(void);

#ifdef __cplusplus
}
#endif
