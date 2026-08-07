#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t diagnostics_init(void);

void diagnostics_note_command(const char *name);

const char *diagnostics_last_command(void);
uint32_t diagnostics_uptime_sec(void);
const char *diagnostics_firmware_version(void);

#ifdef __cplusplus
}
#endif
