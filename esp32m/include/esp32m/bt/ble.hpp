#pragma once

#include "esp32m/device.hpp"
#include "esp32m/events/response.hpp"

#include <ArduinoJson.h>
#include <freertos/event_groups.h>
#include <vector>

struct ble_gap_event;
struct ble_gap_disc_desc;
struct ble_gatt_error;
struct ble_gatt_svc;
struct ble_gatt_chr;
struct ble_gatt_attr;

namespace esp32m {
  namespace bt {
    enum DiscEntryType { DiscDesc, Service, Characteristric, Data };

    class DiscEntry {
     public:
      DiscEntry(const DiscEntry &) = delete;
      DiscEntry(const ble_gap_disc_desc &d);
      DiscEntry(const ble_gatt_svc &d);
      DiscEntry(const ble_gatt_chr &d);
      DiscEntry(const ble_gatt_attr &d);
      ~DiscEntry() {
        free(_data);
      }
      DiscEntryType type() const {
        return _type;
      }
      const void *data() const {
        return _data;
      }

     private:
      DiscEntryType _type;
      void *_data;
    };

    class Ble : public Device {
     public:
      Ble(const Ble &) = delete;
      static Ble &instance();
      const char *name() const override {
        return "ble";
      }

     protected:
      /*DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg, DynamicJsonDocument **result)
      override; DynamicJsonDocument *getConfig(const JsonVariantConst args)
      override;*/
      bool handleRequest(Request &req) override;
      void handleEvent(Event &ev) override;

     private:
      enum PendingReqType {
        None,
        Scan,
        Connect,
        DiscSvcs,
        DiscChrs,
        Read,
        Write
      };
      TaskHandle_t _task = nullptr;
      EventGroupHandle_t _eventGroup = nullptr;
      Response *_pendingResponse = nullptr;
      std::vector<std::unique_ptr<DiscEntry> > _disc;
      PendingReqType _reqType = PendingReqType::None;
      Ble(){};
      bool init();
      void run();
      void onSync();
      void onReset(int reason);
      int gapEvent(ble_gap_event *event);
      int onDiscSvc(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_svc *service);
      int onDiscChr(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_chr *chr);
      int onReadWrite(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_attr *chr);
      esp_err_t discStart(Request &req);
      esp_err_t connectStart(Request &req);
      esp_err_t discSvcsStart(Request &req);
      esp_err_t discChrsStart(Request &req);
      esp_err_t read(Request &req);
      esp_err_t write(Request &req);
      friend void on_sync();
      friend void on_reset(int reason);
      friend int gap_event(ble_gap_event *event, void *arg);
      friend int on_disc_svc(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_svc *service, void *arg);
      friend int on_disc_chr(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg);
      friend int on_read_write(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr, void *arg);
    };

    void useBle();

  }  // namespace bt

}  // namespace esp32m
