#pragma once

#include <memory>
#include <mutex>

#include "esp32m/config/store.hpp"

namespace esp32m {

  class Config : public virtual log::Loggable {
   public:
    static const char *KeyConfigGet;
    static const char *KeyConfigSet;

    Config(ConfigStore *store) : _store(store){};
    Config(const Config &) = delete;
    const char *name() const override {
      return "config";
    }
    void save();
    void load();
    void reset();
    DynamicJsonDocument *read();

   private:
    std::unique_ptr<ConfigStore> _store;
    std::mutex _mutex;
  };

}  // namespace esp32m