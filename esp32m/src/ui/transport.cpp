#include "esp32m/ui/transport.hpp"
#include "esp32m/json.hpp"
#include "esp32m/ui.hpp"

namespace esp32m {
  namespace ui {

    void Transport::incoming(uint32_t cid, void *data, size_t len) {
      if (!len || !data)
        return;
      DynamicJsonDocument *dp = json::parse((const char *)data, len);
      _ui->incoming(cid, dp);
    }
    void Transport::incoming(uint32_t cid, DynamicJsonDocument *json) {
      _ui->incoming(cid, json);
    }

    void Transport::sessionClosed(uint32_t cid) {
      _ui->sessionClosed(cid);
    }

  }  // namespace ui
}  // namespace esp32m