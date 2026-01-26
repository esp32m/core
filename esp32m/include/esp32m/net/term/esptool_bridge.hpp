#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <rfc2217_server.h>

#include "esp32m/app.hpp"
#include "esp32m/net/term/rfc2217_uart_server.hpp"
#include "esp32m/net/term/uart.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      struct EsptoolBridgeConfig {
        EsptoolBridgeConfig() = default;

        uint16_t tcpPort = 3334;

        // Target control pins on the PROXY.
        // - enGpio drives target EN/CHIP_PU (reset)
        // - bootGpio drives target GPIO0 (boot strap)
        int enGpio = -1;
        int bootGpio = -1;

        // Most ESP auto-reset circuits are active-low.
        bool enActiveLow = true;
        bool bootActiveLow = true;

        // Some boards wire the classic auto-reset circuit such that DTR and RTS
        // roles are swapped. Set this to true if the target won't enter ROM
        // download mode with the default mapping.
        bool swapControlLines = false;

        // RFC2217 control semantics are "signal asserted/deasserted" (DTR/RTS),
        // but many ESP auto-reset circuits expect the *electrical* DTR/RTS pins
        // from a USB-UART adapter (where "asserted" often corresponds to an
        // active-low output). If your proxy GPIO drives a transistor network
        // designed for those adapters, you may need to invert the meaning of
        // DTR and/or RTS here so that a client holding RTS=1 doesn't keep the
        // target in reset.
        bool invertDtr = false;
        bool invertRts = false;

        // Optional: ignore client-driven DTR/RTS changes.
        // This is useful when `enterDownloadModeOnSessionStart` is enabled,
        // because esptool/pyserial may toggle control lines after connect and
        // accidentally reboot the target out of ROM download mode.
        bool ignoreClientControl = false;
        uint32_t ignoreClientControlMs = 0;

        // Optional: perform a local reset pulse to enter ROM download mode when
        // a client session starts. This makes the sequence deterministic and
        // avoids relying on client-side DTR/RTS timing.
        bool enterDownloadModeOnSessionStart = false;
        uint32_t enterDownloadModeResetHoldMs = 60;
        uint32_t enterDownloadModeBootHoldMs = 120;

        // Optional delay after the pulse before allowing client data to flow to
        // UART. Useful when the target needs extra time to come out of reset or
        // when the client starts sending sync bytes very quickly.
        uint32_t postPulseDelayMs = 0;

        // For RC/capacitor-based auto-reset circuits, BOOT may only be pulled
        // low briefly on an edge. This sets how long (in microseconds) BOOT is
        // asserted *before* releasing EN, so that the BOOT-low pulse overlaps
        // the EN release.
        uint32_t bootAssertLeadUs = 0;

        // Optional: automatically sweep a set of EN/BOOT timings on session
        // start and detect ROM loader entry by sending a valid SYNC command
        // and waiting for a correct response.
        //
        // This is useful when the proxy drives a transistor+RC auto-reset
        // circuit where correct ROM entry depends on precise edge overlap.
        bool autoTuneDownloadModeOnSessionStart = false;
        uint32_t autoTuneMaxTimeMs = 1500;
        uint32_t autoTuneProbeTimeoutMs = 200;
        uint32_t autoTuneInterAttemptDelayMs = 40;
        uint32_t autoTuneResetHoldMs = 80;
        uint32_t autoTuneBootHoldMs = 120;
        uint32_t autoTunePostPulseDelayMs = 40;

        // If true, keep BOOT (GPIO0) asserted after the pulse for the whole
        // session. This prevents accidental exit from download mode on targets
        // with weak pull-ups or noisy control wiring.
        bool holdBootWhileSessionActive = false;

        // Keep the physical UART baudrate at or above this value. pyserial may
        // negotiate 9600 early; dropping the physical baudrate can break the
        // ESP ROM loader sync.
        uint32_t minPhysicalBaudrate = 115200;

        // Debug: after the download-mode pulse, sample UART RX for a short time
        // and log the first bytes (if any). In true ROM download mode the chip
        // is typically silent until it receives a sync.
        bool debugDumpUartAfterPulse = false;
        uint32_t debugDumpUartAfterPulseMs = 250;

        // Optional: apply UART config when a session starts.
        bool applyUartConfig = false;
        UartConfig uartConfig;

        // Task/server settings
        size_t taskStackSize = 4096;
        unsigned taskPriority = 5;
        unsigned taskCoreId = 0;
      };

      class EsptoolBridge : public AppObject {
       public:
        explicit EsptoolBridge(const EsptoolBridgeConfig &cfg);
        EsptoolBridge(uint16_t tcpPort, int enGpio, int bootGpio);

        EsptoolBridge(const EsptoolBridge &) = delete;
        EsptoolBridge &operator=(const EsptoolBridge &) = delete;

        const char *name() const override {
          return "uart-esptool";
        }

       private:
        void initPins();
        void applyControl(bool dtrAsserted, bool rtsAsserted);
        void setTargetLines(bool bootAsserted, bool enAsserted);

        void enterDownloadModePulse();
        void enterDownloadModePulse(uint32_t bootAssertLeadUs, bool keepBootAfterPulse);

        void drainUartRx(uint32_t maxTimeMs);
        bool probeRomWithSync(uint32_t timeoutMs);
        bool autoTuneDownloadMode();

        static rfc2217_control_t onControlThunk(void *ctx, rfc2217_control_t requested);
        rfc2217_control_t onControl(rfc2217_control_t requested);

        static unsigned onBaudrateThunk(void *ctx, unsigned baudrate);
        unsigned onBaudrate(unsigned baudrate);

        static void onSessionStartedThunk(void *ctx);
        static void onSessionEndedThunk(void *ctx);
        void onSessionStarted();
        void onSessionEnded();

        EsptoolBridgeConfig _cfg;
        std::unique_ptr<Rfc2217UartServer> _server;

        SemaphoreHandle_t _mutex = nullptr;
        bool _dtr = false;
        bool _rts = false;

        TickType_t _ignoreControlUntil = 0;

        uint32_t _physicalBaudrate = 115200;
      };

    }  // namespace term
  }  // namespace net
}  // namespace esp32m