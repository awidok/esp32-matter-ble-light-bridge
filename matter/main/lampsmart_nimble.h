// Portions derived from QB4-dev/esp-lampsmart-ble 1.0.3 (MIT).
// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include "esp_err.h"
#include "sdkconfig.h"

namespace lampsmart {

constexpr uint16_t kControllerId =
    static_cast<uint16_t>(CONFIG_LAMPSMART_CONTROLLER_ID);
constexpr uint8_t kGroupIndex =
    static_cast<uint8_t>(CONFIG_LAMPSMART_GROUP_INDEX);

/** Broadcast one decoded LampSmart v1a command using Matter's NimBLE host. */
esp_err_t send_raw(uint8_t command, uint8_t parameter = 0, uint8_t arg0 = 0,
                   uint8_t arg1 = 0, uint8_t arg2 = 0);

esp_err_t set_power(bool on);
esp_err_t set_cww(uint8_t cold, uint8_t warm);
esp_err_t set_rgb(uint8_t red, uint8_t green, uint8_t blue);

} // namespace lampsmart
