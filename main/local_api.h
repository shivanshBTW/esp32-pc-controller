#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Authenticated LAN HTTP API. Starts only after networking is up. */
esp_err_t local_api_start(void);

#ifdef __cplusplus
}
#endif
