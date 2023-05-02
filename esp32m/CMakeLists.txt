set(CMAKE_CXX_STANDARD 20)

idf_component_register(
    SRC_DIRS "src" "src/events" "src/log" "src/config" "src/bus" "src/net" "src/fs" "src/io" "src/bt" "src/ui" "src/dev" "src/dev/opentherm" "src/debug"
    INCLUDE_DIRS "include"
    REQUIRES idf::nvs_flash idf::bootloader_support idf::app_update idf::spiffs idf::esp_http_client idf::esp_https_ota idf::esp_http_server idf::console idf::esp_wifi idf::esp_adc idf::mqtt idf::spi_flash idf::driver idf::bt idf::wpa_supplicant idf::esp_eth
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
                   WORKING_DIRECTORY "${web_ui_build_dir}"
)

add_custom_target(esp32m_web_ui DEPENDS ${web_ui_resources}
                   WORKING_DIRECTORY "${web_ui_build_dir}"
)
