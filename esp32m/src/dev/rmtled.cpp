#include "esp32m/dev/rmtled.hpp"
#include "esp32m/base.hpp"
#include "esp32m/debug/diag.hpp"
#include "esp32m/io/gpio.hpp"
#include <esp_task_wdt.h>
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "RMTLED";


// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)


static uint8_t s_led_state = 0;

namespace esp32m {
  namespace dev {
    Rmtled::Rmtled(gpio_num_t pin) {
      _pin=pin;
      init();
    }


    void Rmtled::configure_led() {
      ESP_LOGI(TAG, "Initialising addressable LED");
      /* LED strip initialization with the GPIO and pixels number*/
      led_strip_config_t strip_config = {
        .strip_gpio_num = _pin,                   // The GPIO that connected to the LED strip's data line
        .max_leds = 1,                            // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags = { 
            .invert_out = false,                  // whether to invert the output signal
          }
      };

     // LED strip backend configuration: RMT
      led_strip_rmt_config_t rmt_config = {
          .clk_src = RMT_CLK_SRC_DEFAULT,         // different clock source can lead to different power consumption
          .resolution_hz = LED_STRIP_RMT_RES_HZ,  // RMT counter clock frequency
          .mem_block_symbols=64,
          .flags = {
              .with_dma = false,                  // DMA feature is available on ESP target like ESP32-S3
            }
      };
      
      ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
      ESP_LOGI(TAG, "Created LED strip object with RMT backend");

    }


    void Rmtled::init() {
      configure_led();
      xTaskCreate([](void *self) { ((Rmtled *)self)->run(); }, "m/rmtled", 2048,
                  this, tskIDLE_PRIORITY + 1, &_task);
    }

    void Rmtled::toggle_led()
      {
          /* If the addressable LED is enabled */
          if (s_led_state) {
              /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
              led_strip_set_pixel(led_strip, 0, 0, 24, 48);
              /* Refresh the strip to send data */
              led_strip_refresh(led_strip);
          } else {
              /* Set all LED off to clear all pixels */
              led_strip_clear(led_strip);
          }
          s_led_state = !s_led_state;
      }


    void Rmtled::blink(int count) {

        for (int i = 0; i < count; i++) {
          toggle_led();
          delay(200);
          toggle_led();
          delay(100);
        }
      
    }


    void Rmtled::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        for (int i = 0; i < 11; i++) {
          blink(i);
          delay(750);
        }
        delay(2000);
      }
    }


    Rmtled *useRmtled(gpio_num_t pin) {
      return new Rmtled(pin);
    }

  }  // namespace debug
}  // namespace esp32m
