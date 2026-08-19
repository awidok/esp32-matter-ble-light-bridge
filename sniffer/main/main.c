/*
 * Portions derived from QB4-dev/esp-lampsmart-ble 1.0.3 (MIT).
 * SPDX-FileCopyrightText: 2026 awidok
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_ble_api.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#define MAX_ADV_LEN 62

static const uint8_t s_xboxes[128] = {
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16,
};

static esp_ble_scan_params_t s_scan_params = {
    .scan_type = BLE_SCAN_TYPE_PASSIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x50,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static uint8_t s_last_packet[MAX_ADV_LEN];
static uint8_t s_last_packet_len;
static int64_t s_last_print_us;

static const char *command_name(uint8_t command)
{
    switch (command) {
    case 0x10:
        return "on";
    case 0x11:
        return "off";
    case 0x12:
        return "rgb-on";
    case 0x13:
        return "rgb-off";
    case 0x1E:
        return "rgb-effect-on";
    case 0x1F:
        return "rgb-effect-off";
    case 0x21:
        return "cct-level";
    case 0x22:
        return "rgb";
    case 0x23:
        return "night";
    case 0x28:
        return "pair";
    case 0x31:
        return "fan";
    case 0x32:
        return "fan-6-speed";
    case 0x33:
        return "fan-preset";
    case 0x41:
    case 0x51:
        return "timer";
    case 0x45:
        return "unpair";
    case 0x6F:
        return "all-off";
    default:
        return "other";
    }
}

static int find_bytes(const uint8_t *data, size_t data_len, const uint8_t *needle,
                      size_t needle_len)
{
    if (needle_len == 0 || data_len < needle_len) {
        return -1;
    }

    for (size_t i = 0; i <= data_len - needle_len; ++i) {
        if (memcmp(&data[i], needle, needle_len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint16_t crc16_be(const uint8_t *data, size_t length, uint16_t seed)
{
    uint16_t crc = seed;

    for (size_t i = 0; i < length; ++i) {
        crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) == 0U ? (uint16_t)(crc << 1)
                                        : (uint16_t)((crc << 1) ^ 0x1021U);
        }
    }
    return crc;
}

static void v2_whiten(uint8_t *data, uint8_t length, uint8_t seed)
{
    for (uint8_t i = 0; i < length; ++i) {
        uint8_t index = (uint8_t)((seed + i + 9U) & 0x1FU);
        data[i] ^= s_xboxes[index];
        data[i] ^= seed;
    }
}

static void ble_whiten(uint8_t *data, size_t start, size_t length, uint8_t seed)
{
    uint8_t lfsr = seed;

    for (size_t i = 0; i < start + length; ++i) {
        uint8_t mask = 0;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            lfsr <<= 1;
            if ((lfsr & 0x80U) != 0U) {
                lfsr ^= 0x11U;
                mask |= (uint8_t)(1U << bit);
            }
            lfsr &= 0x7FU;
        }
        if (i >= start) {
            data[i - start] ^= mask;
        }
    }
}

static uint8_t reverse_byte(uint8_t value)
{
    value = (uint8_t)(((value & 0x55U) << 1) | ((value & 0xAAU) >> 1));
    value = (uint8_t)(((value & 0x33U) << 2) | ((value & 0xCCU) >> 2));
    return (uint8_t)(((value & 0x0FU) << 4) | ((value & 0xF0U) >> 4));
}

static void print_raw(const uint8_t *data, size_t length)
{
    printf("raw=");
    for (size_t i = 0; i < length; ++i) {
        printf("%02X", data[i]);
    }
    putchar('\n');
}

static bool decode_v3_v2(const uint8_t *data, size_t length)
{
    static const uint8_t marker[] = {0xF0, 0x08};
    int marker_pos = find_bytes(data, length, marker, sizeof(marker));
    if (marker_pos < 0 || (size_t)marker_pos + 26U > length) {
        return false;
    }

    uint8_t packet[26];
    memcpy(packet, &data[marker_pos], sizeof(packet));

    uint16_t seed = (uint16_t)packet[22] | ((uint16_t)packet[23] << 8);
    uint16_t received_crc = (uint16_t)packet[24] | ((uint16_t)packet[25] << 8);
    uint16_t expected_crc = crc16_be(&packet[2], 22, (uint16_t)~seed);

    v2_whiten(&packet[4], 18, (uint8_t)seed);

    uint32_t group_id = (uint32_t)packet[8] | ((uint32_t)packet[9] << 8) |
                        ((uint32_t)packet[10] << 16) | ((uint32_t)packet[11] << 24);
    uint16_t signature = (uint16_t)packet[19] | ((uint16_t)packet[20] << 8);
    uint8_t group_index = packet[12];
    uint8_t command = packet[13];

    printf("FOUND variant=%s id=0x%08" PRIX32 " index=%u command=%s(0x%02X) "
           "param=%u args=[%u,%u,%u] tx=%u crc=%s\n",
           signature == 0 ? "v2" : "v3", group_id, group_index,
           command_name(command), command, packet[15], packet[16], packet[17],
           packet[18], packet[5], expected_crc == received_crc ? "ok" : "bad");
    print_raw(data, length);
    return true;
}

static bool decode_v1(const uint8_t *data, size_t length, bool variant_b)
{
    static const uint8_t marker_a[] = {0x1B, 0x03, 0x77, 0xF8};
    static const uint8_t marker_b[] = {0x1B, 0x03, 0xF9, 0x08, 0x49};
    static const uint8_t base_header[] = {0xAA, 0x98, 0x43, 0xAF, 0x0B, 0x46, 0x46, 0x46};
    const uint8_t *marker = variant_b ? marker_b : marker_a;
    size_t marker_len = variant_b ? sizeof(marker_b) : sizeof(marker_a);
    size_t base_len = variant_b ? 23U : 24U;
    size_t whitening_start = variant_b ? 16U : 15U;
    int marker_pos = find_bytes(data, length, marker, marker_len);

    if (marker_pos < 0 || (size_t)marker_pos + marker_len + base_len > length) {
        return false;
    }

    uint8_t base[24];
    memcpy(base, &data[marker_pos + marker_len], base_len);
    ble_whiten(base, whitening_start, base_len, 83U);
    for (size_t i = 0; i < base_len; ++i) {
        base[i] = reverse_byte(base[i]);
    }

    if (memcmp(base, base_header, sizeof(base_header)) != 0) {
        printf("FOUND possible variant=%s, but decode header did not validate\n",
               variant_b ? "v1b" : "v1a");
        print_raw(data, length);
        return true;
    }

    uint8_t command = base[8];
    uint16_t group_index = (uint16_t)base[9] | ((uint16_t)base[10] << 8);
    uint16_t id = group_index & 0xF0FFU;
    uint8_t index = (uint8_t)((group_index >> 8) & 0x0FU);
    printf("FOUND variant=%s id=0x%04X index=%u wire-group=0x%04X "
           "command=%s(0x%02X) param=%u args=[%u,%u,%u] tx=%u\n",
           variant_b ? "v1b" : "v1a", id, index, group_index,
           command_name(command), command, base[15], base[11], base[12],
           base[13], base[14]);
    print_raw(data, length);
    return true;
}

static void inspect_advertisement(const esp_ble_gap_cb_param_t *param)
{
    const uint8_t *data = param->scan_rst.ble_adv;
    uint8_t length = param->scan_rst.adv_data_len;
    int64_t now_us = esp_timer_get_time();

    if (length == 0 || length > MAX_ADV_LEN) {
        return;
    }

    if (length == s_last_packet_len && memcmp(data, s_last_packet, length) == 0 &&
        now_us - s_last_print_us < 1000000) {
        return;
    }

    bool found = decode_v3_v2(data, length) || decode_v1(data, length, false) ||
                 decode_v1(data, length, true);
    if (found) {
        memcpy(s_last_packet, data, length);
        s_last_packet_len = length;
        s_last_print_us = now_us;
    }
}

static void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_ERROR_CHECK(esp_ble_gap_start_scanning(0));
        } else {
            printf("ERROR: scan parameter setup failed: %d\n", param->scan_param_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            puts("Listening. Use any button in the remote or LampSmart app.");
        } else {
            printf("ERROR: scan start failed: %d\n", param->scan_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            inspect_advertisement(param);
        }
        break;
    default:
        break;
    }
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    init_nvs();

    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_callback));

    puts("\nLampSmart BLE sniffer ready.");
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&s_scan_params));
}
