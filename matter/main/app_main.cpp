// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#include <cstring>
#include <cstdio>

#include "app/server/CommissioningWindowManager.h"
#include "app/server/Server.h"
#include "platform/ConfigurationManager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_matter.h"
#include "esp_matter_console.h"
#include "lamp_bridge.h"
#include "nvs_flash.h"

#include "common_macros.h"

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::endpoint;

namespace {

constexpr char kTag[] = "app_main";
constexpr uint16_t kMinMireds = 153;
constexpr uint16_t kMaxMireds = 370;
constexpr auto kCommissioningTimeout = chip::System::Clock::Seconds16(300);
uint16_t s_light_endpoint_id;

void app_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(kTag, "Matter commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(kTag, "Matter commissioning session started");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(kTag, "Matter commissioning session stopped");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
            ESP_LOGI(kTag, "LampSmart bridge is active");
            lamp_bridge_sync_from_matter(s_light_endpoint_id);
        }
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(kTag, "Matter commissioning failed: fail-safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(kTag, "Matter fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &manager = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!manager.IsCommissioningWindowOpen()) {
                const CHIP_ERROR err = manager.OpenBasicCommissioningWindow(
                    kCommissioningTimeout, chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(kTag, "Cannot open commissioning window: %" CHIP_ERROR_FORMAT,
                             err.Format());
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

esp_err_t identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                            uint8_t effect_id, uint8_t effect_variant, void *)
{
    ESP_LOGI(kTag, "Identify: type=%u endpoint=%u effect=%u variant=%u", type,
             endpoint_id, effect_id, effect_variant);
    return ESP_OK;
}

esp_err_t attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                              uint32_t cluster_id, uint32_t attribute_id,
                              esp_matter_attr_val_t *value, void *)
{
    if (type != attribute::PRE_UPDATE) {
        return ESP_OK;
    }
    return lamp_bridge_attribute_update(endpoint_id, cluster_id, attribute_id, value);
}

void enable_deferred_persistence(uint32_t cluster_id, uint32_t attribute_id)
{
    attribute_t *attr = attribute::get(s_light_endpoint_id, cluster_id, attribute_id);
    if (attr != nullptr) {
        attribute::set_deferred_persistence(attr);
    }
}

} // namespace

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    node::config_t node_config;
    uint8_t wifi_mac[6] = {};
    ESP_ERROR_CHECK(esp_read_mac(wifi_mac, ESP_MAC_WIFI_STA));

    char unique_id[sizeof(node_config.root_node.basic_information.unique_id)] = {};
    std::snprintf(unique_id, sizeof(unique_id), "lamp-%02x%02x%02x%02x%02x%02x",
                  wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4],
                  wifi_mac[5]);
    char serial_number[24] = {};
    std::snprintf(serial_number, sizeof(serial_number), "LAMP-%02X%02X%02X%02X%02X%02X",
                  wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4],
                  wifi_mac[5]);
    const CHIP_ERROR serial_error =
        chip::DeviceLayer::ConfigurationMgr().StoreSerialNumber(serial_number,
                                                                 std::strlen(serial_number));
    ABORT_APP_ON_FAILURE(serial_error == CHIP_NO_ERROR,
                         ESP_LOGE(kTag, "Failed to store serial number: %" CHIP_ERROR_FORMAT,
                                  serial_error.Format()));

    std::strncpy(node_config.root_node.basic_information.node_label,
                 "LampSmart Bridge",
                 sizeof(node_config.root_node.basic_information.node_label) - 1);
    std::strncpy(node_config.root_node.basic_information.unique_id,
                 unique_id,
                 sizeof(node_config.root_node.basic_information.unique_id) - 1);
    node_t *node = node::create(&node_config, attribute_update_cb, identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(kTag, "Failed to create Matter node"));

    endpoint_t *root_endpoint = endpoint::get(node, 0);
    cluster_t *basic_information_cluster =
        cluster::get(root_endpoint, BasicInformation::Id);
    attribute_t *serial_number_attribute =
        cluster::basic_information::attribute::create_serial_number(
            basic_information_cluster, serial_number, std::strlen(serial_number));
    ABORT_APP_ON_FAILURE(serial_number_attribute != nullptr,
                         ESP_LOGE(kTag, "Failed to create serial-number attribute"));

    extended_color_light::config_t light_config;
    light_config.on_off.on_off = false;
    light_config.on_off_lighting.start_up_on_off = nullptr;
    light_config.level_control.current_level = 128;
    light_config.level_control.on_level = 128;
    light_config.level_control_lighting.start_up_current_level = 128;
    light_config.color_control.color_mode =
        static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
    light_config.color_control.enhanced_color_mode =
        static_cast<uint8_t>(ColorControl::ColorMode::kColorTemperature);
    light_config.color_control_color_temperature.color_temperature_mireds = 250;
    light_config.color_control_color_temperature.color_temp_physical_min_mireds = kMinMireds;
    light_config.color_control_color_temperature.color_temp_physical_max_mireds = kMaxMireds;
    light_config.color_control_color_temperature.couple_color_temp_to_level_min_mireds = kMinMireds;
    light_config.color_control_color_temperature.start_up_color_temperature_mireds = nullptr;

    endpoint_t *endpoint = extended_color_light::create(node, &light_config,
                                                         ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(endpoint != nullptr,
                         ESP_LOGE(kTag, "Failed to create extended-color light"));

    // The standard extended-color endpoint has XY + color temperature. Alice
    // also uses the optional Hue/Saturation feature for its colour controls.
    cluster_t *color_control_cluster = cluster::get(endpoint, ColorControl::Id);
    cluster::color_control::feature::hue_saturation::config_t hue_saturation_config;
    hue_saturation_config.current_hue = 0;
    hue_saturation_config.current_saturation = 254;
    ESP_ERROR_CHECK(cluster::color_control::feature::hue_saturation::add(
        color_control_cluster, &hue_saturation_config));

    s_light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(kTag, "LampSmart Matter light endpoint: %u", s_light_endpoint_id);

    enable_deferred_persistence(LevelControl::Id,
                                LevelControl::Attributes::CurrentLevel::Id);
    enable_deferred_persistence(ColorControl::Id,
                                ColorControl::Attributes::CurrentX::Id);
    enable_deferred_persistence(ColorControl::Id,
                                ColorControl::Attributes::CurrentY::Id);
    enable_deferred_persistence(ColorControl::Id,
                                ColorControl::Attributes::CurrentHue::Id);
    enable_deferred_persistence(ColorControl::Id,
                                ColorControl::Attributes::CurrentSaturation::Id);
    enable_deferred_persistence(ColorControl::Id,
                                ColorControl::Attributes::ColorTemperatureMireds::Id);

    ESP_ERROR_CHECK(lamp_bridge_init(s_light_endpoint_id));
    ESP_ERROR_CHECK(esp_matter::start(app_event_cb));

    // On a commissioned reboot, restore the persisted Matter state to the lamp.
    // On a fresh device we deliberately leave the physical lamp untouched while
    // Matter owns BLE for commissioning.
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
        ESP_ERROR_CHECK(lamp_bridge_sync_from_matter(s_light_endpoint_id));
    }

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif
}
