// Portions derived from QB4-dev/esp-lampsmart-ble 1.0.3 (MIT).
// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#include "lampsmart_nimble.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"

namespace lampsmart {
namespace {

constexpr char kTag[] = "lampsmart";
constexpr size_t kPacketLength = 31;
constexpr uint32_t kAdvertisingDurationMs = 400;
constexpr uint8_t kCommandOn = 0x10;
constexpr uint8_t kCommandOff = 0x11;
constexpr uint8_t kCommandCww = 0x21;
constexpr uint8_t kCommandRgb = 0x22;

uint8_t s_tx_count;

uint16_t crc16_be(const uint8_t *data, size_t length, uint16_t seed)
{
    uint16_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                      : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

uint8_t reverse_byte(uint8_t value)
{
    value = static_cast<uint8_t>(((value & 0x55U) << 1) | ((value & 0xAAU) >> 1));
    value = static_cast<uint8_t>(((value & 0x33U) << 2) | ((value & 0xCCU) >> 2));
    return static_cast<uint8_t>(((value & 0x0FU) << 4) | ((value & 0xF0U) >> 4));
}

void whiten(uint8_t *data, size_t start, size_t length, uint8_t seed)
{
    uint8_t lfsr = seed;
    for (size_t i = 0; i < start + length; ++i) {
        uint8_t mask = 0;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            lfsr <<= 1;
            if ((lfsr & 0x80U) != 0U) {
                lfsr ^= 0x11U;
                mask |= static_cast<uint8_t>(1U << bit);
            }
            lfsr &= 0x7FU;
        }
        if (i >= start) {
            data[i - start] ^= mask;
        }
    }
}

std::array<uint8_t, kPacketLength> build_packet(uint8_t command, uint8_t parameter,
                                                 uint8_t arg0, uint8_t arg1,
                                                 uint8_t arg2)
{
    std::array<uint8_t, kPacketLength> packet{};
    constexpr uint8_t advertising_header[] = {0x02, 0x01, 0x02, 0x1B, 0x03, 0x77, 0xF8};
    constexpr uint8_t protocol_header[] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};
    constexpr size_t offset = sizeof(advertising_header);

    std::memcpy(packet.data(), advertising_header, sizeof(advertising_header));
    uint8_t *base = packet.data() + offset;
    std::memcpy(base, protocol_header, sizeof(protocol_header));

    const uint16_t seed = static_cast<uint16_t>(esp_random() % 65525U);
    const uint16_t group = static_cast<uint16_t>((kControllerId & 0xF0FFU) |
                                                 ((kGroupIndex & 0x0FU) << 8));

    if (++s_tx_count == 0) {
        ++s_tx_count;
    }

    base[8] = command;
    base[9] = static_cast<uint8_t>(group & 0xFFU);
    base[10] = static_cast<uint8_t>(group >> 8);
    base[11] = arg0;
    base[12] = arg1;
    base[13] = command == kCommandRgb ? arg2 : 0;
    base[14] = s_tx_count;
    base[15] = parameter;
    base[16] = static_cast<uint8_t>(seed ^ 1U);
    base[17] = static_cast<uint8_t>(seed ^ 1U);
    base[18] = static_cast<uint8_t>(seed >> 8);
    base[19] = static_cast<uint8_t>(seed & 0xFFU);

    const uint16_t inner_crc = crc16_be(base + 8, 12, static_cast<uint16_t>(~seed));
    base[20] = static_cast<uint8_t>(inner_crc >> 8);
    base[21] = static_cast<uint8_t>(inner_crc & 0xFFU);

    const uint16_t outer_seed = crc16_be(base + 1, 5, 0xFFFFU);
    const uint16_t outer_crc = crc16_be(base + 8, 14, outer_seed);
    packet[29] = static_cast<uint8_t>(outer_crc >> 8);
    packet[30] = static_cast<uint8_t>(outer_crc & 0xFFU);

    for (size_t i = offset; i < packet.size(); ++i) {
        packet[i] = reverse_byte(packet[i]);
    }
    whiten(packet.data() + offset, offset + 8, packet.size() - offset, 83U);
    return packet;
}

int advertising_event(struct ble_gap_event *, void *)
{
    return 0;
}

esp_err_t advertise(const std::array<uint8_t, kPacketLength> &packet)
{
    // Matter owns the legacy advertiser during BLE commissioning. Never stop
    // or replace it; wait until it naturally becomes free.
    for (unsigned attempt = 0; attempt < 50; ++attempt) {
        if (ble_hs_synced() && !ble_gap_adv_active()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!ble_hs_synced() || ble_gap_adv_active()) {
        ESP_LOGW(kTag, "NimBLE advertiser is busy (Matter commissioning may be active)");
        return ESP_ERR_TIMEOUT;
    }

    int rc = ble_gap_adv_set_data(packet.data(), static_cast<int>(packet.size()));
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_gap_adv_set_data failed: %d", rc);
        return ESP_FAIL;
    }

    uint8_t own_addr_type = 0;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_hs_id_infer_auto failed: %d", rc);
        return ESP_FAIL;
    }

    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_NON;
    params.disc_mode = BLE_GAP_DISC_MODE_NON;
    params.itvl_min = 0x20;
    params.itvl_max = 0x20;

    rc = ble_gap_adv_start(own_addr_type, nullptr, kAdvertisingDurationMs,
                           &params, advertising_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    // A command is intentionally repeated in advertisements for reliability.
    vTaskDelay(pdMS_TO_TICKS(kAdvertisingDurationMs + 40));
    return ESP_OK;
}

} // namespace

esp_err_t send_raw(uint8_t command, uint8_t parameter, uint8_t arg0,
                   uint8_t arg1, uint8_t arg2)
{
    ESP_LOGI(kTag, "TX command=0x%02x param=0x%02x args=[%u,%u,%u] group=0x%04x",
             command, parameter, arg0, arg1, arg2,
             static_cast<unsigned>((kControllerId & 0xF0FFU) | (kGroupIndex << 8)));
    return advertise(build_packet(command, parameter, arg0, arg1, arg2));
}

esp_err_t set_power(bool on)
{
    return send_raw(on ? kCommandOn : kCommandOff);
}

esp_err_t set_cww(uint8_t cold, uint8_t warm)
{
    return send_raw(kCommandCww, 0, cold, warm, 0);
}

esp_err_t set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    return send_raw(kCommandRgb, 0, red, green, blue);
}

} // namespace lampsmart
