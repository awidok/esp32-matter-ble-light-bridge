/*
 * SPDX-FileCopyrightText: 2026 awidok
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lampsmart_ble.h"

#define INPUT_BUFFER_SIZE 128
#define MAX_ARGS 6

static lampsmart_ble_t s_light;
static lampsmart_ble_config_t s_config;

static const char *variant_name(lampsmart_variant_t variant)
{
    switch (variant) {
    case LAMPSMART_VARIANT_3:
        return "v3";
    case LAMPSMART_VARIANT_2:
        return "v2";
    case LAMPSMART_VARIANT_1A:
        return "v1a";
    case LAMPSMART_VARIANT_1B:
        return "v1b";
    default:
        return "unknown";
    }
}

static bool parse_variant(const char *text, lampsmart_variant_t *variant)
{
    if (strcmp(text, "v3") == 0) {
        *variant = LAMPSMART_VARIANT_3;
    } else if (strcmp(text, "v2") == 0) {
        *variant = LAMPSMART_VARIANT_2;
    } else if (strcmp(text, "v1a") == 0) {
        *variant = LAMPSMART_VARIANT_1A;
    } else if (strcmp(text, "v1b") == 0) {
        *variant = LAMPSMART_VARIANT_1B;
    } else {
        return false;
    }
    return true;
}

static bool parse_number(const char *text, long minimum, long maximum, long *value)
{
    char *end = NULL;
    long parsed = strtol(text, &end, 0);

    if (text[0] == '\0' || end == NULL || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }

    *value = parsed;
    return true;
}

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);

    if (text[0] == '\0' || end == NULL || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static void print_help(void)
{
    puts("Commands:");
    puts("  pair                 pair using the selected variant");
    puts("  pair-all             pair using v3, v2, v1a and v1b");
    puts("  variant <v3|v2|v1a|v1b>");
    puts("  on | off");
    puts("  level <cold 0..255> <warm 0..255>");
    puts("  cold <0..255> | warm <0..255> | neutral <0..255>");
    puts("  secondary-on | secondary-off   model-specific 0x12/0x13");
    puts("  rgb <red> <green> <blue>, each 0..255");
    puts("  effect <on|off>      built-in RGB colour cycle");
    puts("  night                built-in night-light preset");
    puts("  brighter | dimmer | cooler | warmer");
    puts("  raw <cmd> <param> <arg0> <arg1> <arg2>");
    puts("  reverse <on|off>     swap cold and warm channels");
    puts("  duration <100..5000> advertising duration in ms");
    puts("  group <number>       controller ID, decimal or 0xHEX");
    puts("  index <0..15>        subgroup/index nibble");
    puts("  status | help");
}

static void print_status(void)
{
    printf("id=0x%08" PRIX32 " index=%u wire-group=0x%04" PRIX16
           " variant=%s duration=%" PRIu32 "ms reversed=%s\n",
           s_config.group_id, s_config.group_index,
           (uint16_t)((s_config.group_id & 0xF0FFU) |
                      ((uint16_t)(s_config.group_index & 0x0FU) << 8)),
           variant_name(s_config.variant), s_config.tx_duration_ms,
           s_config.reversed_channels ? "on" : "off");
}

static void report_result(const char *action, esp_err_t err)
{
    if (err == ESP_OK) {
        printf("OK: %s queued\n", action);
    } else {
        printf("ERROR: %s: %s\n", action, esp_err_to_name(err));
    }
}

static void apply_config(void)
{
    ESP_ERROR_CHECK(lampsmart_ble_set_config(s_light, &s_config));
}

static void pair_all_variants(void)
{
    const lampsmart_variant_t variants[] = {
        LAMPSMART_VARIANT_3,
        LAMPSMART_VARIANT_2,
        LAMPSMART_VARIANT_1A,
        LAMPSMART_VARIANT_1B,
    };
    lampsmart_ble_config_t saved = s_config;

    // Four packets at 700 ms fit comfortably in the lamp's approximately
    // five-second pairing window. Packets are fully encoded before queuing,
    // so restoring the selected config below does not alter queued jobs.
    s_config.tx_duration_ms = 700;
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i) {
        s_config.variant = variants[i];
        apply_config();
        esp_err_t err = lampsmart_ble_pair(s_light);
        if (err != ESP_OK) {
            printf("ERROR: pair %s: %s\n", variant_name(variants[i]), esp_err_to_name(err));
            break;
        }
        printf("Queued pair %s\n", variant_name(variants[i]));
    }

    s_config = saved;
    apply_config();
    puts("Pair sequence queued. Wait four seconds before sending another command.");
}

static int split_args(char *line, char *args[MAX_ARGS])
{
    int argc = 0;
    char *save = NULL;
    char *token = strtok_r(line, " \t\r\n", &save);

    while (token != NULL && argc < MAX_ARGS) {
        for (char *p = token; *p != '\0'; ++p) {
            *p = (char)tolower((unsigned char)*p);
        }
        args[argc++] = token;
        token = strtok_r(NULL, " \t\r\n", &save);
    }
    return argc;
}

static void handle_command(char *line)
{
    char *args[MAX_ARGS] = {0};
    int argc = split_args(line, args);
    long first = 0;
    long second = 0;
    long third = 0;

    if (argc == 0) {
        return;
    }

    if (strcmp(args[0], "help") == 0) {
        print_help();
    } else if (strcmp(args[0], "status") == 0) {
        print_status();
    } else if (strcmp(args[0], "pair") == 0) {
        report_result("pair", lampsmart_ble_pair(s_light));
    } else if (strcmp(args[0], "pair-all") == 0 || strcmp(args[0], "pairall") == 0) {
        pair_all_variants();
    } else if (strcmp(args[0], "on") == 0) {
        report_result("on", lampsmart_ble_turn_on(s_light));
    } else if (strcmp(args[0], "off") == 0) {
        report_result("off", lampsmart_ble_turn_off(s_light));
    } else if (strcmp(args[0], "secondary-on") == 0) {
        report_result("secondary-on", lampsmart_ble_secondary_set(s_light, true));
    } else if (strcmp(args[0], "secondary-off") == 0) {
        report_result("secondary-off", lampsmart_ble_secondary_set(s_light, false));
    } else if (strcmp(args[0], "rgb") == 0 && argc == 4) {
        if (!parse_number(args[1], 0, 255, &first) ||
            !parse_number(args[2], 0, 255, &second) ||
            !parse_number(args[3], 0, 255, &third)) {
            puts("Usage: rgb <red 0..255> <green 0..255> <blue 0..255>");
            return;
        }
        report_result("rgb", lampsmart_ble_set_rgb(s_light, (uint8_t)first,
                                                     (uint8_t)second, (uint8_t)third));
    } else if (strcmp(args[0], "effect") == 0 && argc == 2) {
        if (strcmp(args[1], "on") == 0) {
            report_result("effect-on", lampsmart_ble_rgb_effect_set(s_light, true));
        } else if (strcmp(args[1], "off") == 0) {
            report_result("effect-off", lampsmart_ble_rgb_effect_set(s_light, false));
        } else {
            puts("Usage: effect <on|off>");
        }
    } else if (strcmp(args[0], "night") == 0) {
        report_result("night", lampsmart_ble_night_mode(s_light));
    } else if (strcmp(args[0], "brighter") == 0) {
        report_result("brighter", lampsmart_ble_send_raw(s_light, 0x21, 0x14, 0, 0, 0));
    } else if (strcmp(args[0], "dimmer") == 0) {
        report_result("dimmer", lampsmart_ble_send_raw(s_light, 0x21, 0x28, 0, 0, 0));
    } else if (strcmp(args[0], "cooler") == 0) {
        report_result("cooler", lampsmart_ble_send_raw(s_light, 0x21, 0x24, 0, 0, 0));
    } else if (strcmp(args[0], "warmer") == 0) {
        report_result("warmer", lampsmart_ble_send_raw(s_light, 0x21, 0x18, 0, 0, 0));
    } else if (strcmp(args[0], "raw") == 0 && argc == 6) {
        long values[5];
        for (int i = 0; i < 5; ++i) {
            if (!parse_number(args[i + 1], 0, 255, &values[i])) {
                puts("Usage: raw <cmd 0..255> <param> <arg0> <arg1> <arg2>");
                return;
            }
        }
        report_result("raw", lampsmart_ble_send_raw(s_light, (uint8_t)values[0],
                                                      (uint8_t)values[1], (uint8_t)values[2],
                                                      (uint8_t)values[3], (uint8_t)values[4]));
    } else if (strcmp(args[0], "variant") == 0 && argc == 2) {
        lampsmart_variant_t variant;
        if (!parse_variant(args[1], &variant)) {
            puts("Usage: variant <v3|v2|v1a|v1b>");
            return;
        }
        s_config.variant = variant;
        apply_config();
        print_status();
    } else if (strcmp(args[0], "level") == 0 && argc == 3) {
        if (!parse_number(args[1], 0, 255, &first) ||
            !parse_number(args[2], 0, 255, &second)) {
            puts("Usage: level <cold 0..255> <warm 0..255>");
            return;
        }
        report_result("level", lampsmart_ble_set_levels(s_light, (uint8_t)first, (uint8_t)second));
    } else if ((strcmp(args[0], "cold") == 0 || strcmp(args[0], "warm") == 0 ||
                strcmp(args[0], "neutral") == 0) && argc == 2) {
        if (!parse_number(args[1], 0, 255, &first)) {
            puts("Level must be between 0 and 255.");
            return;
        }
        uint8_t cold;
        uint8_t warm;
        if (strcmp(args[0], "neutral") == 0) {
            cold = (uint8_t)(first / 2);
            warm = (uint8_t)(first - cold);
        } else {
            cold = strcmp(args[0], "warm") == 0 ? 0 : (uint8_t)first;
            warm = strcmp(args[0], "cold") == 0 ? 0 : (uint8_t)first;
        }
        report_result(args[0], lampsmart_ble_set_levels(s_light, cold, warm));
    } else if (strcmp(args[0], "reverse") == 0 && argc == 2) {
        if (strcmp(args[1], "on") == 0) {
            s_config.reversed_channels = true;
        } else if (strcmp(args[1], "off") == 0) {
            s_config.reversed_channels = false;
        } else {
            puts("Usage: reverse <on|off>");
            return;
        }
        apply_config();
        print_status();
    } else if (strcmp(args[0], "duration") == 0 && argc == 2) {
        if (!parse_number(args[1], 100, 5000, &first)) {
            puts("Usage: duration <100..5000>");
            return;
        }
        s_config.tx_duration_ms = (uint32_t)first;
        apply_config();
        print_status();
    } else if (strcmp(args[0], "group") == 0 && argc == 2) {
        uint32_t group_id = 0;
        if (!parse_u32(args[1], &group_id)) {
            puts("Usage: group <1..0xFFFFFFFF>");
            return;
        }
        s_config.group_id = group_id;
        apply_config();
        print_status();
        puts("Group changed. Pair the lamp again.");
    } else if (strcmp(args[0], "index") == 0 && argc == 2) {
        if (!parse_number(args[1], 0, 15, &first)) {
            puts("Usage: index <0..15>");
            return;
        }
        s_config.group_index = (uint8_t)first;
        apply_config();
        print_status();
        puts("Index changed. Pair the lamp again.");
    } else {
        puts("Unknown command. Type 'help'.");
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
    char input[INPUT_BUFFER_SIZE];
    size_t input_len = 0;

    init_nvs();

    s_config = LAMPSMART_BLE_CONFIG_DEFAULT();
    // Captured from the already-paired physical remote. On v1 the on-air
    // group 0x5118 consists of controller ID 0x5018 plus subgroup index 1.
    s_config.group_id = 0x5018;
    s_config.group_index = 1;
    s_config.variant = LAMPSMART_VARIANT_1A;
    s_config.tx_duration_ms = 800;

    ESP_ERROR_CHECK(lampsmart_ble_init(&s_light, &s_config));

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    puts("\nLampSmart Mac remote is ready.");
    print_status();
    print_help();
    printf("lamp> ");

    while (true) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (input_len > 0) {
                input[input_len] = '\0';
                handle_command(input);
                input_len = 0;
                printf("lamp> ");
            }
        } else if (ch == '\b' || ch == 0x7F) {
            if (input_len > 0) {
                input_len--;
            }
        } else if (input_len + 1 < sizeof(input)) {
            input[input_len++] = (char)ch;
        } else {
            input_len = 0;
            puts("Input line too long; discarded.");
            printf("lamp> ");
        }
    }
}
