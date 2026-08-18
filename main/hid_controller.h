#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HID_KEY_ENTER = 0,
    HID_KEY_ESCAPE,
    HID_KEY_DELETE,
    HID_KEY_BACKSPACE,
    HID_KEY_TAB,
    HID_KEY_UP,
    HID_KEY_DOWN,
    HID_KEY_LEFT,
    HID_KEY_RIGHT,
    HID_KEY_F1,
    HID_KEY_F2,
    HID_KEY_F3,
    HID_KEY_F4,
    HID_KEY_F5,
    HID_KEY_F6,
    HID_KEY_F7,
    HID_KEY_F8,
    HID_KEY_F9,
    HID_KEY_F10,
    HID_KEY_F11,
    HID_KEY_F12,
    HID_COMBO_CTRL_ALT_DELETE,
} hid_command_t;

esp_err_t hid_controller_init(void);

/** Send a predefined key/combo. */
esp_err_t hid_controller_send(hid_command_t cmd);

bool hid_controller_is_ready(void);

#ifdef __cplusplus
}
#endif
