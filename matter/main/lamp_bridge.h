// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_matter.h"

/** Initialize the asynchronous Matter-to-LampSmart state bridge. */
esp_err_t lamp_bridge_init(uint16_t endpoint_id);

/** Consume a successfully committed Matter attribute from POST_UPDATE. */
esp_err_t lamp_bridge_attribute_update(uint16_t endpoint_id, uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *value);

/** Reapply an explicit On/Off command that the SDK would treat as a no-op. */
void lamp_bridge_repeat_power_command(uint16_t endpoint_id, bool on);

/** Reapply an explicit color target when its Matter attributes already match. */
void lamp_bridge_repeat_color_command(uint16_t endpoint_id, uint32_t command_id,
                                      uint16_t first, uint16_t second = 0);

/** Read persisted Matter attributes and apply them to the physical lamp. */
esp_err_t lamp_bridge_sync_from_matter(uint16_t endpoint_id);
