#pragma once

#include <mutex>

#include "esp32m/app.hpp"

namespace esp32m {
  namespace io {
    class Vfs : public AppObject {
     public:
      Vfs(const Vfs &) = delete;
      const char *name() const override {
        return "vfs";
      }

      static Vfs &instance();

     protected:
      bool handleRequest(Request &req) override;

     private:
      Vfs(){};
    };

    Vfs &useVfs();

  }  // namespace io
}  // namespace esp32m
