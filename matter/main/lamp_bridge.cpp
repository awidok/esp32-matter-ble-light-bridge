// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#include "lamp_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lampsmart_nimble.h"

using namespace chip::app::Clusters;
using namespace esp_matter;

namespace {

constexpr char kTag[] = "lamp_bridge";
constexpr uint16_t kMinMireds = 153; // approximately 6500 K
constexpr uint16_t kMaxMireds = 370; // approximately 2700 K

enum class ColorMode : uint8_t { kTemperature, kXy, kHueSaturation };

struct LampState {
    bool power = false;
    uint8_t level = 128;
    ColorMode mode = ColorMode::kTemperature;
    uint16_t mireds = 250;
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t hue = 0;
    uint8_t saturation = 254;
};

uint16_t s_endpoint_id;
TaskHandle_t s_task;
portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
LampState s_state;
LampState s_applied;
bool s_applied_valid;

template <typename T> T clamp_value(T value, T minimum, T maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

uint8_t to_byte(double value)
{
    return static_cast<uint8_t>(std::lround(clamp_value(value, 0.0, 1.0) * 255.0));
}

void hsv_to_rgb(uint8_t matter_hue, uint8_t matter_saturation, uint8_t matter_level,
                uint8_t &red, uint8_t &green, uint8_t &blue)
{
    const double hue = static_cast<double>(matter_hue) * 360.0 / 254.0;
    const double saturation = static_cast<double>(matter_saturation) / 254.0;
    const double value = static_cast<double>(matter_level) / 254.0;
    const double chroma = value * saturation;
    const double h = hue / 60.0;
    const double x = chroma * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
    const double m = value - chroma;

    double r1 = 0;
    double g1 = 0;
    double b1 = 0;
    if (h < 1.0) {
        r1 = chroma; g1 = x;
    } else if (h < 2.0) {
        r1 = x; g1 = chroma;
    } else if (h < 3.0) {
        g1 = chroma; b1 = x;
    } else if (h < 4.0) {
        g1 = x; b1 = chroma;
    } else if (h < 5.0) {
        r1 = x; b1 = chroma;
    } else {
        r1 = chroma; b1 = x;
    }
    red = to_byte(r1 + m);
    green = to_byte(g1 + m);
    blue = to_byte(b1 + m);
}

double gamma_correct(double value)
{
    return value <= 0.0031308 ? 12.92 * value
                              : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

void xy_to_rgb(uint16_t matter_x, uint16_t matter_y, uint8_t matter_level,
               uint8_t &red, uint8_t &green, uint8_t &blue)
{
    const double x = static_cast<double>(matter_x) / 65536.0;
    const double y = static_cast<double>(matter_y) / 65536.0;
    if (y <= 0.0001) {
        red = green = blue = 0;
        return;
    }

    const double luminance = static_cast<double>(matter_level) / 254.0;
    const double tristimulus_x = luminance * x / y;
    const double tristimulus_z = luminance * std::max(0.0, 1.0 - x - y) / y;

    double r = 3.2406 * tristimulus_x - 1.5372 * luminance - 0.4986 * tristimulus_z;
    double g = -0.9689 * tristimulus_x + 1.8758 * luminance + 0.0415 * tristimulus_z;
    double b = 0.0557 * tristimulus_x - 0.2040 * luminance + 1.0570 * tristimulus_z;
    r = std::max(0.0, r);
    g = std::max(0.0, g);
    b = std::max(0.0, b);

    const double peak = std::max({r, g, b});
    if (peak > 1.0) {
        r /= peak;
        g /= peak;
        b /= peak;
    }
    red = to_byte(gamma_correct(r));
    green = to_byte(gamma_correct(g));
    blue = to_byte(gamma_correct(b));
}

bool equal_state(const LampState &left, const LampState &right)
{
    return left.power == right.power && left.level == right.level &&
           left.mode == right.mode && left.mireds == right.mireds &&
           left.x == right.x && left.y == right.y && left.hue == right.hue &&
           left.saturation == right.saturation;
}

esp_err_t apply_state(const LampState &state)
{
    if (!state.power) {
        ESP_LOGI(kTag, "Matter -> LampSmart: off");
        return lampsmart::set_power(false);
    }

    esp_err_t err = ESP_OK;
    if (!s_applied_valid || !s_applied.power) {
        err = lampsmart::set_power(true);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (state.mode == ColorMode::kTemperature) {
        const uint16_t mireds = clamp_value(state.mireds, kMinMireds, kMaxMireds);
        const uint8_t total = static_cast<uint8_t>(
            std::lround(static_cast<double>(state.level) * 255.0 / 254.0));
        const double warm_ratio = static_cast<double>(mireds - kMinMireds) /
                                  static_cast<double>(kMaxMireds - kMinMireds);
        const uint8_t warm = static_cast<uint8_t>(std::lround(total * warm_ratio));
        const uint8_t cold = static_cast<uint8_t>(total - warm);
        ESP_LOGI(kTag, "Matter -> LampSmart: CWW cold=%u warm=%u level=%u mireds=%u",
                 cold, warm, state.level, mireds);
        return lampsmart::set_cww(cold, warm);
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (state.mode == ColorMode::kXy) {
        xy_to_rgb(state.x, state.y, state.level, red, green, blue);
    } else {
        hsv_to_rgb(state.hue, state.saturation, state.level, red, green, blue);
    }
    ESP_LOGI(kTag, "Matter -> LampSmart: RGB [%u,%u,%u] level=%u", red, green,
             blue, state.level);
    return lampsmart::set_rgb(red, green, blue);
}

void bridge_task(void *)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Matter color commands update multiple attributes separately. Debounce
        // them so the lamp receives one coherent final command.
        vTaskDelay(pdMS_TO_TICKS(140));
        while (ulTaskNotifyTake(pdTRUE, 0) != 0) {
        }

        LampState requested;
        taskENTER_CRITICAL(&s_state_lock);
        requested = s_state;
        taskEXIT_CRITICAL(&s_state_lock);

        if (s_applied_valid && equal_state(requested, s_applied)) {
            continue;
        }

        const esp_err_t err = apply_state(requested);
        if (err == ESP_OK) {
            s_applied = requested;
            s_applied_valid = true;
        } else {
            ESP_LOGW(kTag, "LampSmart update failed: %s; retrying", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
            xTaskNotifyGive(s_task);
        }
    }
}

void schedule_apply()
{
    if (s_task != nullptr) {
        xTaskNotifyGive(s_task);
    }
}

esp_err_t read_attribute(uint16_t endpoint_id, uint32_t cluster_id,
                         uint32_t attribute_id)
{
    attribute_t *attr = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (attr == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_matter_attr_val_t value;
    esp_err_t err = attribute::get_val(attr, &value);
    if (err != ESP_OK) {
        return err;
    }
    return lamp_bridge_attribute_update(endpoint_id, cluster_id, attribute_id, &value);
}

} // namespace

esp_err_t lamp_bridge_init(uint16_t endpoint_id)
{
    s_endpoint_id = endpoint_id;
    if (xTaskCreate(bridge_task, "lamp_bridge", 6144, nullptr, 5, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t lamp_bridge_attribute_update(uint16_t endpoint_id, uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *value)
{
    if (endpoint_id != s_endpoint_id || value == nullptr) {
        return ESP_OK;
    }

    bool changed = false;
    taskENTER_CRITICAL(&s_state_lock);
    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        s_state.power = value->val.b;
        changed = true;
    } else if (cluster_id == LevelControl::Id &&
               attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        s_state.level = value->val.u8;
        changed = true;
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            s_state.mireds = value->val.u16;
            s_state.mode = ColorMode::kTemperature;
            changed = true;
        } else if (attribute_id == ColorControl::Attributes::CurrentX::Id) {
            s_state.x = value->val.u16;
            s_state.mode = ColorMode::kXy;
            changed = true;
        } else if (attribute_id == ColorControl::Attributes::CurrentY::Id) {
            s_state.y = value->val.u16;
            s_state.mode = ColorMode::kXy;
            changed = true;
        } else if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            s_state.hue = value->val.u8;
            s_state.mode = ColorMode::kHueSaturation;
            changed = true;
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            s_state.saturation = value->val.u8;
            s_state.mode = ColorMode::kHueSaturation;
            changed = true;
        }
    }
    taskEXIT_CRITICAL(&s_state_lock);

    if (changed) {
        schedule_apply();
    }
    return ESP_OK;
}

esp_err_t lamp_bridge_sync_from_matter(uint16_t endpoint_id)
{
    esp_err_t result = ESP_OK;
    auto read = [&](uint32_t cluster, uint32_t attribute_id) {
        const esp_err_t err = read_attribute(endpoint_id, cluster, attribute_id);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            result = err;
        }
    };

    read(LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);

    attribute_t *mode_attr = attribute::get(endpoint_id, ColorControl::Id,
                                             ColorControl::Attributes::ColorMode::Id);
    esp_matter_attr_val_t mode_value;
    if (mode_attr != nullptr && attribute::get_val(mode_attr, &mode_value) == ESP_OK) {
        const auto mode = static_cast<ColorControl::ColorMode>(mode_value.val.u8);
        if (mode == ColorControl::ColorMode::kColorTemperature) {
            read(ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        } else if (mode == ColorControl::ColorMode::kCurrentXAndCurrentY) {
            read(ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
            read(ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
        } else if (mode == ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
            read(ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
            read(ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        }
    }

    // Power is intentionally last, so the debounce applies a complete state.
    read(OnOff::Id, OnOff::Attributes::OnOff::Id);
    return result;
}
