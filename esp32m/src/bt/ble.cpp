#include "sdkconfig.h"

#ifdef CONFIG_BT_ENABLED

#  include "esp32m/app.hpp"
#  include "esp32m/defs.hpp"

#  include "esp32m/bt/ble.hpp"
#  include "esp32m/bt/utils.hpp"

#  include <ArduinoJson.h>
#  include <esp_nimble_hci.h>
#  include <esp_task_wdt.h>
#  include <host/ble_gatt.h>
#  include <host/ble_hs.h>
#  include <host/util/util.h>
#  include <mbedtls/base64.h>
#  include <nimble/nimble_port.h>
#  include <memory>
#  include "nimble/nimble_port_freertos.h"

extern "C" void ble_store_config_init(void);

namespace esp32m {
  namespace bt {
    DiscEntry::DiscEntry(const ble_gap_disc_desc &d)
        : _type(DiscEntryType::DiscDesc) {
      ble_gap_disc_desc *disc = (ble_gap_disc_desc *)malloc(
          sizeof(ble_gap_disc_desc) + d.length_data);
      memcpy((uint8_t *)disc, (const uint8_t *)(&d), sizeof(ble_gap_disc_desc));
      if (d.length_data) {
        disc->data = ((uint8_t *)disc) + sizeof(ble_gap_disc_desc);
        memcpy((void *)disc->data, (const uint8_t *)d.data, d.length_data);
      }
      _data = disc;
    }

    DiscEntry::DiscEntry(const ble_gatt_svc &d)
        : _type(DiscEntryType::Service) {
      ble_gatt_svc *svc = (ble_gatt_svc *)malloc(sizeof(ble_gatt_svc));
      *svc = d;
      _data = svc;
    }

    DiscEntry::DiscEntry(const ble_gatt_chr &d)
        : _type(DiscEntryType::Characteristric) {
      ble_gatt_chr *chr = (ble_gatt_chr *)malloc(sizeof(ble_gatt_chr));
      *chr = d;
      _data = chr;
    }

    struct gatt_value {
      uint16_t handle;
      uint16_t offset;
      size_t size;
      uint8_t data[0];
    };

    DiscEntry::DiscEntry(const ble_gatt_attr &d) : _type(DiscEntryType::Data) {
      auto om = d.om;
      size_t dlen = 0;
      while (om) {
        dlen += om->om_len;
        om = SLIST_NEXT(om, om_next);
      }
      gatt_value *value = (gatt_value *)malloc(sizeof(gatt_value) + dlen);
      value->handle = d.handle;
      value->offset = d.offset;
      value->size = dlen;
      if (dlen) {
        om = d.om;
        uint8_t *dptr = value->data;
        while (om) {
          memcpy(dptr, om->om_data, om->om_len);
          dptr += om->om_len;
          om = SLIST_NEXT(om, om_next);
        }
      }
      _data = value;
    }

    enum BleFlags {
      Synced = BIT0,
      AsyncStarted = BIT1,
      AsyncDone = BIT2,
      AsyncPartial = BIT3,
      AsyncCancel = BIT4,
    };

    Ble &Ble::instance() {
      static Ble i;
      return i;
    }

    const char *UuidFormat =
        "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%"
        "02hhx%"
        "02hhx%02hhx%02hhx%02hhx%02hhx";

    uint8_t type2len(const ble_uuid_t &uuid) {
      switch (uuid.type) {
        case BLE_UUID_TYPE_16:
          return 4;
        case BLE_UUID_TYPE_32:
          return 8;
        case BLE_UUID_TYPE_128:
          return 36;
      }
      return 0;
    }

    char *toStr(const ble_uuid_t &uuid, char *dst) {
      const uint8_t *u8p;

      switch (uuid.type) {
        case BLE_UUID_TYPE_16:
          sprintf(dst, "%04" PRIx16, BLE_UUID16(&uuid)->value);
          break;
        case BLE_UUID_TYPE_32:
          sprintf(dst, "%08" PRIx32, BLE_UUID32(&uuid)->value);
          break;
        case BLE_UUID_TYPE_128:
          u8p = BLE_UUID128(&uuid)->value;
          sprintf(dst, UuidFormat, u8p[15], u8p[14], u8p[13], u8p[12], u8p[11],
                  u8p[10], u8p[9], u8p[8], u8p[7], u8p[6], u8p[5], u8p[4],
                  u8p[3], u8p[2], u8p[1], u8p[0]);
          break;
        default:
          dst[0] = '\0';
          break;
      }
      return dst;
    }

    esp_err_t fromStr(ble_uuid_any_t &uuid, const char *buf) {
      size_t l = buf ? strlen(buf) : 0;
      const uint8_t *u8p;
      switch (l) {
        case 4:
          if (sscanf(buf, "%04" PRIx16, &uuid.u16.value) != 1)
            break;
          uuid.u.type = BLE_UUID_TYPE_16;
          return ESP_OK;
        case 8:
          if (sscanf(buf, "%08" PRIx32, &uuid.u32.value) != 1)
            break;
          uuid.u.type = BLE_UUID_TYPE_32;
          return ESP_OK;
        case 36:
          u8p = uuid.u128.value;
          if (sscanf(buf, UuidFormat, &u8p[15], &u8p[14], &u8p[13], &u8p[12],
                     &u8p[11], &u8p[10], &u8p[9], &u8p[8], &u8p[7], &u8p[6],
                     &u8p[5], &u8p[4], &u8p[3], &u8p[2], &u8p[1],
                     &u8p[0]) != 16)
            break;
          uuid.u.type = BLE_UUID_TYPE_128;
          return ESP_OK;
      }
      return ESP_ERR_INVALID_ARG;
    }

    bool isnull(const ble_addr_t &a) {
      const ble_addr_t z = {};
      return !memcmp(&z, &a, sizeof(ble_addr_t));
    }

    void on_reset(int reason) {
      Ble::instance().onReset(reason);
    }

    void on_sync(void) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(ble_hs_util_ensure_addr(0));
      Ble::instance().onSync();
    }

    void host_task(void *param) {
      nimble_port_run();
      nimble_port_freertos_deinit();
    }

    int gap_event(ble_gap_event *event, void *arg) {
      return ((Ble *)arg)->gapEvent(event);
    }

    int on_disc_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_svc *service, void *arg) {
      return ((Ble *)arg)->onDiscSvc(conn_handle, error, service);
    }
    int on_disc_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                    const struct ble_gatt_chr *chr, void *arg) {
      return ((Ble *)arg)->onDiscChr(conn_handle, error, chr);
    }
    int on_read_write(uint16_t conn_handle, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg) {
      return ((Ble *)arg)->onReadWrite(conn_handle, error, attr);
    }

    void Ble::onSync() {
      xEventGroupSetBits(_eventGroup, BleFlags::Synced);
      xTaskNotifyGive(_task);
    }

    void Ble::onReset(int reason) {
      xEventGroupClearBits(_eventGroup, BleFlags::Synced);
      loge("resetting state; reason=%d", reason);
    }

    bool Ble::init() {
      ESP_CHECK_RETURN_BOOL(esp_nimble_hci_init());
      nimble_port_init();
      ble_hs_cfg.reset_cb = on_reset;
      ble_hs_cfg.sync_cb = on_sync;
      ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
      ble_store_config_init();
      nimble_port_freertos_init(host_task);
      return true;
    }

    esp_err_t Ble::discStart(Request &req) {
      xEventGroupClearBits(_eventGroup,
                           BleFlags::AsyncStarted | BleFlags::AsyncDone);
      JsonVariantConst data = req.data();
      uint8_t own_addr_type;
      /* Figure out address to use while advertising (no privacy for now) */
      ESP_CHECK_RETURN(ble_hs_id_infer_auto(0, &own_addr_type));

      ble_gap_disc_params disc_params = {
          .itvl = data["itvl"].as<uint16_t>(),
          .window = data["window"].as<uint16_t>(),
          .filter_policy = 0,
          .limited = data["limited"].as<bool>() ? (uint8_t)1 : (uint8_t)0,
          .passive = data["active"].as<bool>() ? (uint8_t)0 : (uint8_t)1,
          .filter_duplicates = 1,
      };

      ESP_CHECK_RETURN(ble_gap_disc(own_addr_type,
                                    data["duration"] | BLE_HS_FOREVER,
                                    &disc_params, gap_event, this));
      return ESP_OK;
    }

    esp_err_t Ble::connectStart(Request &req) {
      xEventGroupClearBits(_eventGroup,
                           BleFlags::AsyncStarted | BleFlags::AsyncDone);
      JsonVariantConst data = req.data();
      ble_addr_t addr;
      ESP_CHECK_RETURN(parse(data["addr"], addr));
      uint8_t own_addr_type;
      ESP_CHECK_RETURN(ble_hs_id_infer_auto(0, &own_addr_type));
      auto err = ble_gap_connect(own_addr_type, &addr, data["duration"] | 30000,
                                 NULL, gap_event, this);
      if (err == BLE_HS_EDONE) {
        ble_gap_conn_desc desc;
        ESP_CHECK_RETURN(ble_gap_conn_find_by_addr(&addr, &desc));
        json::Value v(desc.conn_handle);
        req.respond(v.variant(), false);
        return ESP_OK;
      } else
        ESP_CHECK_RETURN(err);
      return err;
    }

    esp_err_t Ble::discSvcsStart(Request &req) {
      xEventGroupClearBits(_eventGroup,
                           BleFlags::AsyncStarted | BleFlags::AsyncDone);
      JsonVariantConst data = req.data();
      uint16_t conn = data["conn"];
      const char *uuidstr = data["uuid"];
      if (uuidstr) {
        ble_uuid_any_t uuid;
        ESP_CHECK_RETURN(fromStr(uuid, uuidstr));
        ESP_CHECK_RETURN(
            ble_gattc_disc_svc_by_uuid(conn, &uuid.u, on_disc_svc, this));
      } else {
        ESP_CHECK_RETURN(ble_gattc_disc_all_svcs(conn, on_disc_svc, this));
      }
      return ESP_OK;
    }

    esp_err_t Ble::discChrsStart(Request &req) {
      xEventGroupClearBits(_eventGroup,
                           BleFlags::AsyncStarted | BleFlags::AsyncDone);
      JsonVariantConst data = req.data();
      uint16_t conn = data["conn"];
      uint16_t start = data["start"];
      uint16_t end = data["end"];
      const char *uuidstr = data["uuid"];
      if (uuidstr) {
        ble_uuid_any_t uuid;
        ESP_CHECK_RETURN(fromStr(uuid, uuidstr));
        ESP_CHECK_RETURN(ble_gattc_disc_chrs_by_uuid(conn, start, end, &uuid.u,
                                                     on_disc_chr, this));
      } else {
        ESP_CHECK_RETURN(
            ble_gattc_disc_all_chrs(conn, start, end, on_disc_chr, this));
      }
      return ESP_OK;
    }

    esp_err_t Ble::read(Request &req) {
      xEventGroupClearBits(_eventGroup,
                           BleFlags::AsyncStarted | BleFlags::AsyncDone);
      JsonVariantConst data = req.data();
      uint16_t conn = data["conn"];
      uint16_t attr = data["attr"];
      const char *uuidstr = data["uuid"];
      if (uuidstr) {
        uint16_t start = data["start"];
        uint16_t end = data["end"];
        ble_uuid_any_t uuid;
        ESP_CHECK_RETURN(fromStr(uuid, uuidstr));
        ESP_CHECK_RETURN(ble_gattc_read_by_uuid(conn, start, end, &uuid.u,
                                                on_read_write, this));
      } else if (attr) {
        uint16_t offset = data["offset"];
        if (offset)
          ESP_CHECK_RETURN(
              ble_gattc_read_long(conn, attr, offset, on_read_write, this));
        else
          ESP_CHECK_RETURN(ble_gattc_read(conn, attr, on_read_write, this));
      } else
        return ESP_ERR_INVALID_ARG;
      return ESP_OK;
    }

    esp_err_t Ble::write(Request &req) {
      xEventGroupClearBits(_eventGroup,
                           BleFlags::AsyncStarted | BleFlags::AsyncDone);
      JsonVariantConst data = req.data();
      uint16_t conn = data["conn"];
      uint16_t attr = data["attr"];
      if (!attr)
        return ESP_ERR_INVALID_ARG;
      const char *value64 = data["value"];
      auto l = value64 ? strlen(value64) : 0;
      if (!l)
        return ESP_ERR_INVALID_ARG;
      size_t olen;
      mbedtls_base64_decode(nullptr, 0, &olen, (const uint8_t *)value64, l);
      void *buf = malloc(olen);
      ESP_CHECK_RETURN(mbedtls_base64_decode((uint8_t *)buf, l, &olen,
                                             (const uint8_t *)value64, l));
      esp_err_t err;
      /*uint16_t offset = data["offset"];
      if (offset)
      {
          err = ble_gattc_write_long(conn, attr, offset, mbuf, on_read_write,
      this);
      }
      else*/
      err = ble_gattc_write_flat(conn, attr, buf, olen, on_read_write, this);
      free(buf);
      return err;
    }

    void Ble::handleEvent(Event &ev) {
      Device::handleEvent(ev);
      if (EventInit::is(ev, 0)) {
        _eventGroup = xEventGroupCreate();
        if (init())
          xTaskCreate([](void *self) { ((Ble *)self)->run(); }, "m/ble", 4096,
                      this, tskIDLE_PRIORITY + 1, &_task);
      }
    }

    bool Ble::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("cancel")) {
        if (!_pendingResponse) {
          req.respond(ESP_ERR_INVALID_STATE);
          return true;
        }
        xEventGroupSetBits(_eventGroup, BleFlags::AsyncCancel);
        req.respond();
        return true;
      }
      if (_pendingResponse) {
        req.respond(ESP_ERR_INVALID_STATE);
        return true;
      }
      esp_err_t err;
      PendingReqType rt = PendingReqType::None;
      if (req.is("scan")) {
        rt = PendingReqType::Scan;
        err = discStart(req);
      } else if (req.is("connect")) {
        rt = PendingReqType::Connect;
        err = connectStart(req);
      } else if (req.is("disconnect")) {
        err = ble_gap_terminate(req.data()["conn"], 0);
        if (err == ESP_OK)
          req.respond();
      } else if (req.is("services")) {
        rt = PendingReqType::DiscSvcs;
        err = discSvcsStart(req);
      } else if (req.is("characteristics")) {
        rt = PendingReqType::DiscChrs;
        err = discChrsStart(req);
      } else if (req.is("read")) {
        rt = PendingReqType::Read;
        err = read(req);
      } else if (req.is("write")) {
        rt = PendingReqType::Write;
        err = write(req);
      } else
        return false;
      if (err != ESP_OK)
        req.respond(err);
      else if (rt && !req.isResponded()) {
        _reqType = rt;
        xEventGroupClearBits(_eventGroup, BleFlags::AsyncDone);
        xEventGroupSetBits(_eventGroup, BleFlags::AsyncStarted);
        _pendingResponse = req.makeResponse();
        xTaskNotifyGive(_task);
      }
      return true;
    }

    int Ble::gapEvent(ble_gap_event *event) {
      // ble_gap_disc_desc *disc;
      switch (event->type) {
        case BLE_GAP_EVENT_DISC:
          _disc.push_back(
              std::unique_ptr<DiscEntry>(new DiscEntry(event->disc)));
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncPartial);
          break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          xTaskNotifyGive(_task);
          break;
        case BLE_GAP_EVENT_CONNECT:
          if (event->connect.status == 0) {
            logI("connected, handle: %d", event->connect.conn_handle);
            JsonDocument *doc =
                new JsonDocument(); /* JSON_OBJECT_SIZE(1) */
            doc->set(event->connect.conn_handle);
            _pendingResponse->setData(doc);
          } else {
            _pendingResponse->setError(event->connect.status);
          }
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;
        case BLE_GAP_EVENT_DISCONNECT:
          logI("disconnected, handle: %d, reason: %d", event->disconnect.conn,
               event->disconnect.reason);
          break;
      }
      return 0;
    }

    int Ble::onDiscSvc(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *service) {
      switch (error->status) {
        case 0:
          _disc.push_back(std::unique_ptr<DiscEntry>(new DiscEntry(*service)));
          break;

        case BLE_HS_EDONE:
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;

        default:
          _pendingResponse->setError(error->status);
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;
      }
      return 0;
    }

    int Ble::onDiscChr(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr) {
      switch (error->status) {
        case 0:
          _disc.push_back(std::unique_ptr<DiscEntry>(new DiscEntry(*chr)));
          break;

        case BLE_HS_EDONE:
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;

        default:
          _pendingResponse->setError(error->status);
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;
      }
      return 0;
    }

    int Ble::onReadWrite(uint16_t conn_handle,
                         const struct ble_gatt_error *error,
                         const struct ble_gatt_attr *attr) {
      switch (error->status) {
        case 0:
          _disc.push_back(std::unique_ptr<DiscEntry>(new DiscEntry(*attr)));
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;

        default:
          _pendingResponse->setError(error->status);
          xEventGroupSetBits(_eventGroup, BleFlags::AsyncDone);
          break;
      }
      return 0;
    }

    void Ble::run() {
      char addrbuf[37];
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        auto flags = xEventGroupGetBits(_eventGroup);
        if (flags & BleFlags::Synced) {
          if (_pendingResponse &&
              (flags & (BleFlags::AsyncDone | BleFlags::AsyncPartial |
                        BleFlags::AsyncCancel))) {
            bool respond = _pendingResponse->isError();
            bool partial = (flags & BleFlags::AsyncPartial) != 0;
            if (partial)
              xEventGroupClearBits(_eventGroup, BleFlags::AsyncPartial);
            bool cancel = (flags & BleFlags::AsyncCancel) != 0;
            if (cancel) {
              xEventGroupClearBits(_eventGroup, BleFlags::AsyncCancel);
              partial = false;
            }
            size_t count, olen, js;
            JsonDocument *doc;
            JsonArray arr;
            if (!respond)
              switch (_reqType) {
                case PendingReqType::Scan:
                  if (cancel)
                    ble_gap_disc_cancel();
                  count = _disc.size();
                  olen = 0;
                  js = JSON_ARRAY_SIZE(count) + count * JSON_ARRAY_SIZE(5);
                  for (auto it = _disc.begin(); it != _disc.end(); it++) {
                    ble_gap_disc_desc *item =
                        (ble_gap_disc_desc *)(*it)->data();
                    js += 17 + 1;
                    if (!isnull(item->direct_addr))
                      js += 17 + 1;
                    if (item->length_data) {
                      mbedtls_base64_encode(nullptr, 0, &olen, item->data,
                                            item->length_data);
                      js += olen + 1;
                    }
                  }
                  doc = new JsonDocument(); /* js */
                  arr = doc->to<JsonArray>();
                  for (auto it = _disc.begin(); it != _disc.end();) {
                    ble_gap_disc_desc *item =
                        (ble_gap_disc_desc *)(*it)->data();
                    if (format(item->addr, addrbuf) != ESP_OK)
                      continue;
                    auto entry = arr.add<JsonArray>();
                    if (!entry)
                      break;
                    entry.add(item->event_type);
                    entry.add(item->rssi);
                    entry.add(addrbuf);
                    entry.add(isnull(item->direct_addr) ||
                                      format(item->direct_addr, addrbuf) !=
                                          ESP_OK
                                  ? nullptr
                                  : addrbuf);
                    if (item->length_data) {
                      mbedtls_base64_encode(nullptr, 0, &olen, item->data,
                                            item->length_data);
                      char *buf = (char *)malloc(olen + 1);
                      mbedtls_base64_encode((uint8_t *)buf, olen, &olen,
                                            item->data, item->length_data);
                      buf[olen] = 0;
                      entry.add(buf);
                      free(buf);
                    }
                    it = _disc.erase(it);
                  }
                  _pendingResponse->setData(doc);
                  respond = true;
                  break;
                case PendingReqType::Connect:
                  if (cancel)
                    ble_gap_conn_cancel();
                  respond = true;
                  break;
                case PendingReqType::DiscSvcs:
                  count = _disc.size();
                  olen = 0;
                  js = JSON_ARRAY_SIZE(count) + count * JSON_ARRAY_SIZE(3);
                  for (auto it = _disc.begin(); it != _disc.end(); it++) {
                    ble_gatt_svc *item = (ble_gatt_svc *)(*it)->data();
                    js += type2len(item->uuid.u) + 1;
                  }
                  doc = new JsonDocument(); /* js */
                  arr = doc->to<JsonArray>();
                  for (auto it = _disc.begin(); it != _disc.end();) {
                    auto entry = arr.add<JsonArray>();
                    if (!entry)
                      break;
                    ble_gatt_svc *item = (ble_gatt_svc *)(*it)->data();
                    entry.add(item->start_handle);
                    entry.add(item->end_handle);
                    entry.add(toStr(item->uuid.u, addrbuf));
                    it = _disc.erase(it);
                  }
                  _pendingResponse->setData(doc);
                  respond = true;
                  break;
                case PendingReqType::DiscChrs:
                  count = _disc.size();
                  olen = 0;
                  js = JSON_ARRAY_SIZE(count) + count * JSON_ARRAY_SIZE(4);
                  for (auto it = _disc.begin(); it != _disc.end(); it++) {
                    ble_gatt_chr *item = (ble_gatt_chr *)(*it)->data();
                    js += type2len(item->uuid.u) + 1;
                  }
                  doc = new JsonDocument(); /* js */
                  arr = doc->to<JsonArray>();
                  for (auto it = _disc.begin(); it != _disc.end();) {
                    auto entry = arr.add<JsonArray>();
                    if (!entry)
                      break;
                    ble_gatt_chr *item = (ble_gatt_chr *)(*it)->data();
                    entry.add(item->def_handle);
                    entry.add(item->val_handle);
                    entry.add(item->properties);
                    entry.add(toStr(item->uuid.u, addrbuf));
                    it = _disc.erase(it);
                  }
                  _pendingResponse->setData(doc);
                  respond = true;
                  break;
                case PendingReqType::Read:
                case PendingReqType::Write:
                  count = _disc.size();
                  olen = 0;
                  js = JSON_ARRAY_SIZE(count) + count * JSON_ARRAY_SIZE(3);
                  for (auto it = _disc.begin(); it != _disc.end(); it++) {
                    gatt_value *item = (gatt_value *)(*it)->data();
                    mbedtls_base64_encode(nullptr, 0, &olen, item->data,
                                          item->size);
                    js += olen + 1;
                  }
                  doc = new JsonDocument(); /* js */
                  arr = doc->to<JsonArray>();
                  for (auto it = _disc.begin(); it != _disc.end();) {
                    auto entry = arr.add<JsonArray>();
                    if (!entry)
                      break;
                    gatt_value *item = (gatt_value *)(*it)->data();
                    entry.add(item->handle);
                    entry.add(item->offset);
                    if (item->size) {
                      mbedtls_base64_encode(nullptr, 0, &olen, item->data,
                                            item->size);
                      char *buf = (char *)malloc(olen + 1);
                      mbedtls_base64_encode((uint8_t *)buf, olen, &olen,
                                            item->data, item->size);
                      buf[olen] = 0;
                      entry.add(buf);
                      free(buf);
                    }
                    it = _disc.erase(it);
                  }
                  _pendingResponse->setData(doc);
                  respond = true;
                  break;
                default:
                  break;
              }
            if (respond) {
              _pendingResponse->setPartial(partial);
              _pendingResponse->publish();
              if (!partial) {
                delete _pendingResponse;
                _pendingResponse = nullptr;
                xEventGroupClearBits(
                    _eventGroup, BleFlags::AsyncStarted | BleFlags::AsyncDone);
              }
            }
          }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      }
    }

    void useBle() {
      Ble::instance();
    }

  }  // namespace bt
}  // namespace esp32m

#endif