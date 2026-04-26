# Expose shared versioning helpers to the whole build once esp32m is processed.
include("${CMAKE_CURRENT_LIST_DIR}/scripts/versioning.cmake")

idf_component_register(
    SRC_DIRS "src" "src/events" "src/log" "src/config" "src/bus" "src/net" "src/fs" "src/io" "src/net/term" "src/bt" "src/ui" "src/dev" "src/dev/opentherm" "src/debug" "src/debug/lwip" "src/integrations/ha" "src/integrations/influx"
    INCLUDE_DIRS "include"
    REQUIRES esp_common nvs_flash bootloader_support app_update spiffs esp_http_client esp_https_ota esp_http_server console esp_wifi esp_adc spi_flash driver bt wpa_supplicant esp_eth esp_netif espcoredump
             esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc esp_driver_rmt esp_driver_spi esp_driver_pcnt esp_ringbuf esp_driver_dac
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++23)

# Redirect _esp_error_check_failed[_without_abort] through esp32m logging so
# that ESP_ERROR_CHECK(_WITHOUT_ABORT) messages reach all registered appenders
# (e.g. UDP/syslog) instead of going only to esp_rom_printf / UART.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=_esp_error_check_failed_without_abort"
    "-Wl,--wrap=_esp_error_check_failed"
)

set(web_ui_build_dir "${CMAKE_BINARY_DIR}/web-ui")
file(MAKE_DIRECTORY "${web_ui_build_dir}")
set(web_ui_resources "dist/index.html.S" "dist/main.js.S")

add_custom_command(OUTPUT ${web_ui_resources}
                   COMMAND "${python}" 
                   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-ui.py" 
                   "--esp32m-dir=${CMAKE_CURRENT_SOURCE_DIR}" 
                   "--source-dir=${CMAKE_SOURCE_DIR}"
                   "--build-dir=${CMAKE_BINARY_DIR}"
                   "--build-mode=${CONFIG_ESP32M_UI_BUILD_MODE}"
                   WORKING_DIRECTORY "${web_ui_build_dir}"
)

add_custom_target(esp32m_web_ui DEPENDS ${web_ui_resources}
                   WORKING_DIRECTORY "${web_ui_build_dir}"
)
