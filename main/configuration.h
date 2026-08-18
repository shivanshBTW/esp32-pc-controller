#pragma once

#include "sdkconfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRODUCT_NAME              "WakeType"
#define FIRMWARE_VERSION          "0.4.0-dev"
#define CONFIG_SCHEMA_VERSION     2

#define AP_SSID_DEFAULT           "WakeType-Setup"
#define HOSTNAME_DEFAULT          "waketype"
#define MDNS_SERVICE_TYPE         "_waketype"
#define MDNS_SERVICE_PROTO        "_tcp"
#define API_HTTP_PORT             80

#define POWER_RELAY_GPIO          CONFIG_PC_POWER_RELAY_GPIO
#define RESET_RELAY_GPIO          CONFIG_PC_RESET_RELAY_GPIO
#define PC_STATE_GPIO             CONFIG_PC_STATE_GPIO
#define RELAY_ACTIVE_LOW          CONFIG_PC_RELAY_ACTIVE_LOW

#define POWER_PRESS_MS_DEFAULT    CONFIG_PC_POWER_PRESS_MS
#define RESET_PRESS_MS_DEFAULT    CONFIG_PC_RESET_PRESS_MS
#define DEFAULT_LONG_PRESS_MS_DEFAULT CONFIG_PC_DEFAULT_LONG_PRESS_MS
#define MAX_RELAY_HOLD_MS         CONFIG_PC_MAX_RELAY_HOLD_MS
#define RELAY_WATCHDOG_MS         (MAX_RELAY_HOLD_MS + 500)

#define WIFI_STA_FAIL_BEFORE_AP   10
#define PC_STATE_ACTIVE_LEVEL     0
#define PC_STATE_SAMPLE_MS        50
#define PC_STATE_STEADY_MS        1500

static inline int relay_active_level(void)
{
    return RELAY_ACTIVE_LOW ? 0 : 1;
}

static inline int relay_inactive_level(void)
{
    return RELAY_ACTIVE_LOW ? 1 : 0;
}

/** True if GPIO is allowed for user-selectable WakeType wiring on ESP32-S3. */
static inline bool waketype_gpio_allowed(int gpio)
{
    if (gpio < 1 || gpio > 48) {
        return false;
    }
    /* USB Serial/JTAG */
    if (gpio == 19 || gpio == 20) {
        return false;
    }
    /* Strapping */
    if (gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46) {
        return false;
    }
    /* Octal flash / PSRAM (WROOM-1-N16R8) */
    if (gpio >= 33 && gpio <= 37) {
        return false;
    }
    /* UART0 console */
    if (gpio == 43 || gpio == 44) {
        return false;
    }
    return true;
}

static inline bool waketype_gpio_trio_ok(int power_gpio, int reset_gpio, int state_gpio)
{
    if (!waketype_gpio_allowed(power_gpio) || !waketype_gpio_allowed(reset_gpio) ||
        !waketype_gpio_allowed(state_gpio)) {
        return false;
    }
    return power_gpio != reset_gpio && power_gpio != state_gpio && reset_gpio != state_gpio;
}

#ifdef __cplusplus
}
#endif
