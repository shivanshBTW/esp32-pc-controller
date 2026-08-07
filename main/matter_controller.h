#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Matter over Wi-Fi (Google Home).
 * Only the safe short power pulse is exposed through Matter.
 * Long-hold / reset / HID stay on the authenticated local API.
 */
esp_err_t matter_controller_start(void);

#ifdef __cplusplus
}
#endif
