/*
 * Derived from QB4-dev/esp-lampsmart-ble 1.0.3, declared MIT upstream.
 * Modifications copyright 2026 awidok.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported LampSmart BLE protocol variants.
 */
typedef enum {
    LAMPSMART_VARIANT_3 = 0,
    LAMPSMART_VARIANT_2 = 1,
    LAMPSMART_VARIANT_1A = 2,
    LAMPSMART_VARIANT_1B = 3,
} lampsmart_variant_t;

/**
 * @brief LampSmart light controller configuration.
 */
typedef struct {
    lampsmart_variant_t variant;           /*!< LampSmart protocol variant */
    uint32_t            group_id;          /*!< Shared LampSmart group identifier */
    uint8_t             group_index;       /*!< Subgroup/index nibble (0..15) */
    bool                reversed_channels; /*!< Swap cold and warm channels in outgoing commands */
    uint8_t             min_brightness;    /*!< Minimum non-zero brightness*/
    uint32_t            tx_duration_ms;    /*!< BLE advertisement duration  */
} lampsmart_ble_config_t;

/**
 * @brief Opaque handle for one LampSmart light controller.
 *
 * Allocate with lampsmart_ble_init(); free with lampsmart_ble_deinit().
 */
typedef void *lampsmart_ble_t;

/** @brief Default LampSmart controller configuration initializer. */
#define LAMPSMART_BLE_CONFIG_DEFAULT()  \
    ((lampsmart_ble_config_t){          \
        .variant = LAMPSMART_VARIANT_3, \
        .group_id = 0x1234,             \
        .group_index = 0,               \
        .reversed_channels = false,     \
        .min_brightness = 1,            \
        .tx_duration_ms = 800U,         \
    })

/**
 * @brief Initialize the BLE stack required by this component.
 *
 * This function configures the ESP-IDF BLE controller and Bluedroid stack.
 * Call nvs_flash_init() in the application before invoking this function.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if Bluetooth is disabled in sdkconfig
 *      - Other ESP-IDF error codes returned by the BLE stack
 */
esp_err_t lampsmart_ble_stack_init(void);

/**
 * @brief Allocate and initialize a LampSmart light controller.
 *
 * @param[out] out_light Set to a newly allocated controller handle on success.
 *                       Free with lampsmart_ble_deinit() when no longer needed.
 * @param[in]  cfg       Controller configuration.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 *      - ESP_ERR_NO_MEM if heap allocation fails
 *      - Other ESP-IDF error codes returned by lampsmart_ble_stack_init()
 */
esp_err_t lampsmart_ble_init(lampsmart_ble_t *out_light, const lampsmart_ble_config_t *cfg);

/**
 * @brief Free a LampSmart controller handle allocated by lampsmart_ble_init().
 *
 * @param[in] light Controller handle to free. May be NULL.
 */
void lampsmart_ble_deinit(lampsmart_ble_t light);

/**
 * @brief Queue a LampSmart pair command for BLE transmission.
 *
 * @param[in,out] light Initialized controller instance.
 *
 * @return
 *      - ESP_OK if the command was queued
 *      - ESP_ERR_TIMEOUT if the internal transmit queue is full
 *      - Other ESP-IDF error codes for invalid state/arguments
 */
esp_err_t lampsmart_ble_pair(lampsmart_ble_t light);

/**
 * @brief Queue a LampSmart unpair command for BLE transmission.
 *
 * @param[in,out] light Initialized controller instance.
 *
 * @return
 *      - ESP_OK if the command was queued
 *      - ESP_ERR_TIMEOUT if the internal transmit queue is full
 *      - Other ESP-IDF error codes for invalid state/arguments
 */
esp_err_t lampsmart_ble_unpair(lampsmart_ble_t light);

/**
 * @brief Queue a LampSmart light-on command for BLE transmission.
 *
 * @param[in,out] light Initialized controller instance.
 *
 * @return
 *      - ESP_OK if the command was queued
 *      - ESP_ERR_TIMEOUT if the internal transmit queue is full
 *      - Other ESP-IDF error codes for invalid state/arguments
 */
esp_err_t lampsmart_ble_turn_on(lampsmart_ble_t light);

/**
 * @brief Queue a LampSmart light-off command for BLE transmission.
 *
 * @param[in,out] light Initialized controller instance.
 *
 * @return
 *      - ESP_OK if the command was queued
 *      - ESP_ERR_TIMEOUT if the internal transmit queue is full
 *      - Other ESP-IDF error codes for invalid state/arguments
 */
esp_err_t lampsmart_ble_turn_off(lampsmart_ble_t light);

/** @brief Queue an on/off command for the secondary (usually RGB) light. */
esp_err_t lampsmart_ble_secondary_set(lampsmart_ble_t light, bool on);

/** @brief Set the secondary light to a raw 8-bit RGB colour. */
esp_err_t lampsmart_ble_set_rgb(lampsmart_ble_t light, uint8_t red, uint8_t green, uint8_t blue);

/** @brief Enable or disable the built-in RGB colour-cycle effect. */
esp_err_t lampsmart_ble_rgb_effect_set(lampsmart_ble_t light, bool on);

/** @brief Activate the lamp's built-in night-light preset. */
esp_err_t lampsmart_ble_night_mode(lampsmart_ble_t light);

/**
 * @brief Send an arbitrary decoded LampSmart command.
 *
 * This is primarily useful while reverse engineering model-specific buttons.
 * For v1 packets, @p parameter occupies the byte called `outs`; command 0x22
 * carries all three RGB arguments. For v2/v3 all fields are encoded directly.
 */
esp_err_t lampsmart_ble_send_raw(lampsmart_ble_t light, uint8_t command,
                                 uint8_t parameter, uint8_t arg0,
                                 uint8_t arg1, uint8_t arg2);

/**
 * @brief Get the current configuration of an initialized controller.
 *
 * @param[in]  light  Initialized controller instance.
 * @param[out] cfg    Set to the current configuration on success.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 */
esp_err_t lampsmart_ble_get_config(lampsmart_ble_t light, lampsmart_ble_config_t *cfg);

/**
 * @brief Replace the configuration of an already-initialized controller.
 *
 * @param[in,out] light Initialized controller instance.
 * @param[in]     cfg   New configuration to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if any argument is NULL
 */
esp_err_t lampsmart_ble_set_config(lampsmart_ble_t light, const lampsmart_ble_config_t *cfg);

/**
 * @brief Set cold and warm white output levels.
 *
 * A `(0, 0)` level request is converted to an off command. For non-zero values,
 * the component automatically queues an on command first when the cached state
 * is off.
 *
 * @param[in,out] light Initialized controller instance.
 * @param[in] cold Cold white level in range 0..255.
 * @param[in] warm Warm white level in range 0..255.
 *
 * @return
 *      - ESP_OK if the required command or commands were queued
 *      - ESP_ERR_TIMEOUT if the internal transmit queue is full
 *      - Other ESP-IDF error codes for invalid state/arguments
 */
esp_err_t lampsmart_ble_set_levels(lampsmart_ble_t light, uint8_t cold, uint8_t warm);

#ifdef __cplusplus
}
#endif
