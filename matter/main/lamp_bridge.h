// SPDX-FileCopyrightText: 2026 awidok
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_matter.h"

/** Initialize the asynchronous Matter-to-LampSmart state bridge. */
esp_err_t lamp_bridge_init(uint16_t endpoint_id);

/** Consume a Matter attribute update. Safe to call from the PRE_UPDATE callback. */
esp_err_t lamp_bridge_attribute_update(uint16_t endpoint_id, uint32_t cluster_id,
                                       uint32_t attribute_id,
                                       esp_matter_attr_val_t *value);

/** Read persisted Matter attributes and apply them to the physical lamp. */
esp_err_t lamp_bridge_sync_from_matter(uint16_t endpoint_id);
