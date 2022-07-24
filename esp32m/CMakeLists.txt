set(CMAKE_CXX_STANDARD 17)

idf_component_register(
    SRC_DIRS "src" "src/events" "src/log" "src/config" "src/bus" "src/net" "src/io" "src/bt" "src/ui" "src/dev" "src/dev/opentherm" "src/debug"
    INCLUDE_DIRS "include" "deps/ArduinoJson/src"
    REQUIRES idf::nvs_flash idf::bootloader_support idf::app_update idf::spiffs idf::esp_http_client idf::esp_https_ota idf::esp_http_server idf::console idf::esp_wifi idf::esp_adc idf::mqtt idf::spi_flash idf::driver idf::bt idf::wpa_supplicant
)

#add_subdirectory("deps/ArduinoJson/src")
 
#target_link_libraries(${COMPONENT_LIB} PUBLIC ArduinoJson)
