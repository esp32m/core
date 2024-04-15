set(CMAKE_CXX_STANDARD 20)

idf_component_register(
    SRC_DIRS "src" "src/events" "src/log" "src/config" "src/bus" "src/net" "src/fs" "src/io" "src/bt" "src/ui" "src/dev" "src/dev/opentherm" "src/debug" "src/debug/lwip" "src/integrations/ha" "src/integrations/influx"
    INCLUDE_DIRS "include"
    REQUIRES esp_common nvs_flash bootloader_support app_update spiffs esp_http_client esp_https_ota esp_http_server console esp_wifi esp_adc mqtt spi_flash driver bt wpa_supplicant esp_eth esp_netif
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
