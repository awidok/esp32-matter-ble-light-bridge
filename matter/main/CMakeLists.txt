idf_component_register(
    SRCS "app_main.cpp" "lamp_bridge.cpp" "lampsmart_nimble.cpp"
    PRIV_INCLUDE_DIRS "." "${ESP_MATTER_PATH}/examples/common/utils"
    PRIV_REQUIRES bt chip esp_matter esp_matter_console esp_system nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
