#pragma once
#include <cstdint>
#include "esp_err.h"

// Only the SDK surface consumed by lamp_bridge.cpp is mocked. Color conversion,
// state selection, debounce/deduplication, and the worker are production code.
namespace chip::app::Clusters {
namespace OnOff {
constexpr uint32_t Id = 6;
namespace Attributes { struct OnOff { static constexpr uint32_t Id = 0; }; }
}
namespace LevelControl {
constexpr uint32_t Id = 8;
namespace Attributes { struct CurrentLevel { static constexpr uint32_t Id = 0; }; }
}
namespace ColorControl {
constexpr uint32_t Id = 0x300;
enum class ColorMode : uint8_t {
    kCurrentHueAndCurrentSaturation = 0, kCurrentXAndCurrentY = 1, kColorTemperature = 2
};
namespace Commands {
struct MoveToHue { static constexpr uint32_t Id = 0; };
struct MoveToSaturation { static constexpr uint32_t Id = 3; };
struct MoveToHueAndSaturation { static constexpr uint32_t Id = 6; };
struct MoveToColor { static constexpr uint32_t Id = 7; };
struct MoveToColorTemperature { static constexpr uint32_t Id = 10; };
}
namespace Attributes {
struct CurrentHue { static constexpr uint32_t Id = 0; };
struct CurrentSaturation { static constexpr uint32_t Id = 1; };
struct CurrentX { static constexpr uint32_t Id = 3; };
struct CurrentY { static constexpr uint32_t Id = 4; };
struct ColorTemperatureMireds { static constexpr uint32_t Id = 7; };
struct ColorMode { static constexpr uint32_t Id = 8; };
}
}
}
struct esp_matter_attr_val_t {
    union { bool b; uint8_t u8; uint16_t u16; } val{};
    bool null_value = false;
    bool is_null() const { return null_value; }
};
namespace esp_matter {
struct attribute_t { esp_matter_attr_val_t value; };
namespace attribute {
attribute_t *get(uint16_t, uint32_t, uint32_t);
esp_err_t get_val(attribute_t *, esp_matter_attr_val_t *);
}
}
