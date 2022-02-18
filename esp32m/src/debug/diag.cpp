#include "esp32m/events/diag.hpp"
#include "esp32m/debug/diag.hpp"
#include "esp32m/events.hpp"

namespace esp32m {
  namespace debug {

    Diag::Diag() {
      EventManager::instance().subscribe([this](Event &ev) {
        event::Diag *diag;
        if (event::Diag::is(ev, &diag)) {
          std::lock_guard guard(_mutex);
          _map[diag->id()] = diag->code();
        }
      });
    }

    int Diag::toArray(uint8_t *arr, size_t size) {
      std::lock_guard guard(_mutex);
      int i = 0;
      for (auto const &it : _map)
        if (i >= size)
          break;
        else if (it.second > 0)
          arr[i++] = it.second;
      return i;
    }

    Diag &Diag::instance() {
      static Diag i;
      return i;
    }

  }  // namespace debug
}  // namespace esp32m
