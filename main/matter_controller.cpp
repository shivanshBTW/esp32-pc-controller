#include "matter_controller.h"
#include "pc_controller.h"
#include "pc_state.h"

#include <app/server/Server.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <cstring>

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static const char *TAG = "matter";

static uint16_t s_endpoint_id;
static bool s_ready;
static char s_manual[32];
static char s_qr[256];
static char s_last_error[48];

static void set_last_error(esp_err_t err)
{
    strlcpy(s_last_error, err == ESP_OK ? "" : esp_err_to_name(err), sizeof(s_last_error));
}

static void cache_pairing_codes(void)
{
    char qr[256] = {0};
    chip::MutableCharSpan qr_span(qr);
    if (GetQRCode(qr_span, chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE)) ==
        CHIP_NO_ERROR) {
        strlcpy(s_qr, qr, sizeof(s_qr));
    }

    char manual[32] = {0};
    chip::MutableCharSpan man_span(manual);
    if (GetManualPairingCode(man_span, chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE)) ==
        CHIP_NO_ERROR) {
        strlcpy(s_manual, manual, sizeof(s_manual));
    }

    ESP_LOGI(TAG, "Pairing code %s", s_manual[0] ? s_manual : "(none)");
    ESP_LOGI(TAG, "QR %s", s_qr[0] ? s_qr : "(none)");
}

static bool pc_is_on(void)
{
    return pc_controller_get_pc_state() == PC_STATE_ON;
}

static void report_onoff(bool on)
{
    if (!s_ready) {
        return;
    }
    esp_matter_attr_val_t val = esp_matter_bool(on);
    attribute::update(s_endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
}

static void matter_state_task(void *arg)
{
    (void)arg;
    bool last = pc_is_on();
    report_onoff(last);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        const bool on = pc_is_on();
        if (on != last) {
            last = on;
            report_onoff(on);
        }
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    (void)endpoint_id;
    (void)priv_data;
    ESP_LOGI(TAG, "Identify type=%u effect=%u variant=%u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    (void)priv_data;
    if (type != PRE_UPDATE || endpoint_id != s_endpoint_id) {
        return ESP_OK;
    }
    if (cluster_id != OnOff::Id || attribute_id != OnOff::Attributes::OnOff::Id || !val) {
        return ESP_OK;
    }

    const bool want_on = val->val.b;
    const bool is_on = pc_is_on();
    ESP_LOGI(TAG, "Matter OnOff want=%d actual=%d", (int)want_on, (int)is_on);

    if (want_on == is_on) {
        return ESP_OK;
    }

    /* Short power press only — never hold or reset. */
    esp_err_t err = pc_controller_power_press(PC_CMD_SOURCE_MATTER);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Matter power press rejected (%s)", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        cache_pairing_codes();
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed (fail-safe expired)");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            ESP_LOGI(TAG, "Last fabric removed — advertising for pairing");
            chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
                chip::System::Clock::Seconds16(900), chip::CommissioningWindowAdvertisement::kAllSupported);
        }
        break;
    default:
        break;
    }
}

static void open_window_work(intptr_t arg)
{
    (void)arg;
    chip::CommissioningWindowManager &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(900),
                                                      chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Open commissioning window failed");
    }
}

extern "C" esp_err_t matter_controller_start(void)
{
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        set_last_error(ESP_FAIL);
        return ESP_FAIL;
    }

    on_off_plugin_unit::config_t plug_config;
    plug_config.on_off.on_off = pc_is_on();
    endpoint_t *endpoint = on_off_plugin_unit::create(node, &plug_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create On/Off plugin endpoint");
        set_last_error(ESP_FAIL);
        return ESP_FAIL;
    }
    s_endpoint_id = endpoint::get_id(endpoint);

    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_matter::start failed (%s)", esp_err_to_name(err));
        set_last_error(err);
        return err;
    }
    set_last_error(ESP_OK);

    cache_pairing_codes();
    s_ready = true;
    xTaskCreate(matter_state_task, "matter_st", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "Matter On/Off plugin ready (endpoint %u) — short powerPress only", s_endpoint_id);
    return ESP_OK;
}

extern "C" bool matter_controller_is_ready(void)
{
    return s_ready;
}

extern "C" bool matter_controller_is_commissioned(void)
{
    if (!s_ready) {
        return false;
    }
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

extern "C" const char *matter_controller_last_error(void)
{
    return s_last_error;
}

extern "C" const char *matter_manual_pairing_code(void)
{
    return s_manual;
}

extern "C" const char *matter_qr_payload(void)
{
    return s_qr;
}

extern "C" esp_err_t matter_controller_open_commissioning_window(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    chip::DeviceLayer::PlatformMgr().ScheduleWork(open_window_work, 0);
    return ESP_OK;
}
