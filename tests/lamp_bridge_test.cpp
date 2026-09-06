// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#include <cassert>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

#include "../matter/main/lamp_bridge.cpp"

namespace {
struct Transmission { int command; int a; int b; int c; };
std::vector<Transmission> transmissions;
std::map<std::tuple<uint16_t, uint32_t, uint32_t>, esp_matter::attribute_t> attributes;
unsigned notifications;
bool worker_ran;
bool fail_tx;
bool fail_read;
struct StopWorker {};

esp_matter_attr_val_t value8(uint8_t v)
{
    esp_matter_attr_val_t value;
    value.val.u8 = v;
    return value;
}
esp_matter_attr_val_t value16(uint16_t v)
{
    esp_matter_attr_val_t value;
    value.val.u16 = v;
    return value;
}
esp_matter_attr_val_t power(bool on)
{
    esp_matter_attr_val_t value;
    value.val.b = on;
    return value;
}
void set(uint32_t cluster, uint32_t attr, esp_matter_attr_val_t value)
{
    assert(lamp_bridge_attribute_update(1, cluster, attr, &value) == ESP_OK);
}
void run_worker()
{
    worker_ran = false;
    try { bridge_task(nullptr); } catch (const StopWorker &) {}
}
void reset()
{
    transmissions.clear();
    attributes.clear();
    s_state = LampState{};
    s_applied = LampState{};
    s_applied_valid = false;
    s_force_apply = false;
    notifications = 0;
    fail_tx = fail_read = false;
    assert(lamp_bridge_init(1) == ESP_OK);
}
void set_mode(ColorControl::ColorMode mode)
{
    set(ColorControl::Id, ColorControl::Attributes::ColorMode::Id,
        value8(static_cast<uint8_t>(mode)));
}
void test_repeat_off_and_on()
{
    reset();
    set(OnOff::Id, OnOff::Attributes::OnOff::Id, power(false));
    run_worker();
    assert(transmissions.size() == 1 && transmissions.back().command == 0x11);
    // The lamp may have been switched on with the remote; Matter still says off.
    // An explicit Off must get through even when the SDK emits no attribute write.
    lamp_bridge_repeat_power_command(1, false);
    run_worker();
    assert(transmissions.size() == 2 && transmissions.back().command == 0x11);

    set(OnOff::Id, OnOff::Attributes::OnOff::Id, power(true));
    run_worker();
    transmissions.clear();
    lamp_bridge_repeat_power_command(1, true);
    run_worker();
    assert(transmissions.size() == 2);
    assert(transmissions[0].command == 0x10 && transmissions[1].command == 0x21);
    // Idle time is not a reason to overwrite a change made with the remote.
    transmissions.clear();
    run_worker();
    assert(transmissions.empty());
}
void test_retry_and_latest_state()
{
    reset();
    set(OnOff::Id, OnOff::Attributes::OnOff::Id, power(false));
    run_worker();
    transmissions.clear();
    fail_tx = true;
    lamp_bridge_repeat_power_command(1, false);
    run_worker();
    assert(s_force_apply && notifications > 0);
    fail_tx = false;
    run_worker();
    assert(transmissions.size() == 2 && transmissions.back().command == 0x11);

    transmissions.clear();
    lamp_bridge_repeat_power_command(1, false);
    set(OnOff::Id, OnOff::Attributes::OnOff::Id, power(true));
    run_worker();
    assert(transmissions.size() == 2 && transmissions[0].command == 0x10);
    // The old pending repeat must not turn the lamp back off.
    lamp_bridge_repeat_power_command(2, true);
    assert(notifications == 0);
}
void test_mode_only_change_and_inactive_attributes()
{
    reset();
    set(OnOff::Id, OnOff::Attributes::OnOff::Id, power(true));
    set(LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, value8(254));
    set(ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id, value16(222));
    set_mode(ColorControl::ColorMode::kCurrentHueAndCurrentSaturation);
    run_worker();
    assert(transmissions.back().command == 0x22);
    assert(transmissions.back().a == 255 && transmissions.back().b == 0);

    // Returning to the same 4500 K only changes ColorMode, not the stored mireds.
    set_mode(ColorControl::ColorMode::kColorTemperature);
    run_worker();
    assert(transmissions.back().command == 0x21);
    assert(transmissions.back().a == 174 && transmissions.back().b == 81);

    set(ColorControl::Id, ColorControl::Attributes::CurrentHue::Id, value8(85));
    set(ColorControl::Id, ColorControl::Attributes::CurrentX::Id, value16(20000));
    run_worker();
    assert(transmissions.back().command == 0x21);
    set(ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id, value16(178));
    run_worker();
    assert(transmissions.back().a == 226 && transmissions.back().b == 29);
    // 5600 K changes temperature, not the requested total brightness.
    assert(transmissions.back().a + transmissions.back().b == 255);
    // Repeating a temperature already stored in Matter must also reach the
    // lamp (e.g. its physical mode was changed by the original remote).
    transmissions.clear();
    lamp_bridge_repeat_color_command(1, ColorControl::Commands::MoveToColorTemperature::Id, 178);
    run_worker();
    assert(transmissions.size() == 2 && transmissions.back().command == 0x21);
    set(OnOff::Id, OnOff::Attributes::OnOff::Id, power(false));
    run_worker();
    transmissions.clear();
    lamp_bridge_repeat_color_command(1, ColorControl::Commands::MoveToColorTemperature::Id, 178);
    run_worker();
    assert(transmissions.empty()); // a color-only request cannot turn Matter on
}
void test_restore_inactive_mode_and_read_failure()
{
    reset();
    attributes[{1, OnOff::Id, OnOff::Attributes::OnOff::Id}].value = power(true);
    attributes[{1, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id}].value = value8(200);
    attributes[{1, ColorControl::Id, ColorControl::Attributes::ColorMode::Id}].value = value8(0);
    attributes[{1, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id}].value = value16(222);
    assert(lamp_bridge_sync_from_matter(1) == ESP_OK);
    run_worker();
    assert(s_state.level == 200 && transmissions.back().command == 0x22);
    set_mode(ColorControl::ColorMode::kColorTemperature);
    run_worker();
    assert(transmissions.back().command == 0x21 && s_state.mireds == 222);
    assert(transmissions.back().a + transmissions.back().b == 201);
    fail_read = true;
    assert(lamp_bridge_sync_from_matter(1) == ESP_FAIL);
    assert(s_state.level == 200 && notifications == 0);
    auto null_level = value8(255);
    null_level.null_value = true;
    set(LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id, null_level);
    assert(s_state.level == 200 && notifications == 0);
}
}

uint32_t ulTaskNotifyTake(int, uint32_t wait)
{
    if (wait == portMAX_DELAY && (worker_ran || notifications == 0)) {
        throw StopWorker{};
    }
    if (wait == portMAX_DELAY) worker_ran = true;
    const unsigned count = notifications;
    notifications = 0;
    return count;
}
void xTaskNotifyGive(TaskHandle_t) { ++notifications; }

namespace esp_matter::attribute {
attribute_t *get(uint16_t ep, uint32_t cluster, uint32_t attr)
{
    auto found = attributes.find({ep, cluster, attr});
    return found == attributes.end() ? nullptr : &found->second;
}
esp_err_t get_val(attribute_t *attr, esp_matter_attr_val_t *value)
{
    if (fail_read) return ESP_FAIL;
    *value = attr->value;
    return ESP_OK;
}
}
namespace lampsmart {
esp_err_t send_raw(uint8_t command, uint8_t, uint8_t a, uint8_t b, uint8_t c)
{
    transmissions.push_back({command, a, b, c});
    return fail_tx ? ESP_FAIL : ESP_OK;
}
esp_err_t set_power(bool on) { return send_raw(on ? 0x10 : 0x11); }
esp_err_t set_cww(uint8_t c, uint8_t w) { return send_raw(0x21, 0, c, w); }
esp_err_t set_rgb(uint8_t r, uint8_t g, uint8_t b) { return send_raw(0x22, 0, r, g, b); }
}

int main()
{
    test_repeat_off_and_on();
    test_retry_and_latest_state();
    test_mode_only_change_and_inactive_attributes();
    test_restore_inactive_mode_and_read_failure();
    std::puts("PASS: repeated power, retry, latest state, ColorMode, CCT brightness, restoration");
}
