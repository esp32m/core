#include "esp32m/dev/rmtled.hpp"
#include "esp32m/base.hpp"
#include "esp32m/debug/diag.hpp"
#include "esp32m/io/gpio.hpp"
#include <esp_task_wdt.h>
#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"


// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

uint32_t led_color[3] = { 16, 0, 16 };  // Red, Green, Blue  (0-255) 


namespace esp32m {
  namespace dev {
    Rmtled::Rmtled(gpio_num_t pin) {
      _pin=pin;
      init();
    }

    void Rmtled::configure_led() {
      logI( "Initialising addressable LED");
      /* LED strip initialization with the GPIO and pixels number*/
      led_strip_config_t strip_config = {
        .strip_gpio_num = _pin,                   
        .max_leds = 1,                            
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, 
        .led_model = LED_MODEL_WS2812,            
        .flags = { 
            .invert_out = false,                  
          }
      };

     // LED strip backend configuration: RMT
      led_strip_rmt_config_t rmt_config = {
          .clk_src = RMT_CLK_SRC_DEFAULT,         
          .resolution_hz = LED_STRIP_RMT_RES_HZ,  
          .mem_block_symbols=64,
          .flags = {
              .with_dma = false,                  // DMA feature is available on ESP target like ESP32-S3
            }
      };
      
      ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
      logI("Created LED strip object with RMT backend");
    }

    void Rmtled::init() {
      configure_led();
      xTaskCreate([](void *self) { ((Rmtled *)self)->run(); }, "m/rmtled", 2048,
                  this, tskIDLE_PRIORITY + 1, &_task);
    }


    void Rmtled::blink(int count) {
      for (int i = 0; i < count; i++) {
          // Set the pixel color 
          led_strip_set_pixel(led_strip, 0, led_color[0],led_color[1], led_color[2]);
          // update strip
          led_strip_refresh(led_strip);
          delay(200);
          // turn off all LEDs in strip
          led_strip_clear(led_strip);
          delay(200);
      }
    }

    void Rmtled::run() {
      uint8_t codes[10];
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        auto c = debug::Diag::instance().toArray(codes, sizeof(codes));
        for (int i = 0; i < c; i++) {
          blink(codes[i]);
          delay(1000);
        }
        delay(1000);
      }
    }

    bool Rmtled::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is(name(), "color")) {
        JsonArrayConst data = req.data();
        if (data) {
         led_color[0]=data[0];
         led_color[1]=data[1];
         led_color[2]=data[2];
        }
      }
      return false;
    }

    Rmtled *useRmtled(gpio_num_t pin) {
      return new Rmtled(pin);
    }

  }  // namespace dev
}  // namespace esp32m
