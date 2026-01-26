#include "esp32m/net/term/esptool_bridge.hpp"

#include <array>

#include <driver/gpio.h>

#include <esp_rom_sys.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>

#include <freertos/task.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      static inline int levelForAsserted(bool asserted, bool activeLow) {
        if (activeLow)
          return asserted ? 0 : 1;
        return asserted ? 1 : 0;
      }

      static portMUX_TYPE s_gpioMux = portMUX_INITIALIZER_UNLOCKED;

      static constexpr uint8_t kSlipEnd = 0xC0;
      static constexpr uint8_t kSlipEsc = 0xDB;
      static constexpr uint8_t kSlipEscEnd = 0xDC;
      static constexpr uint8_t kSlipEscEsc = 0xDD;

      static inline void feedWdt() {
        (void)esp_task_wdt_reset();
      }

      static void delayMsWithWdt(uint32_t ms) {
        if (!ms)
          return;
        const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
        while (xTaskGetTickCount() < until) {
          feedWdt();
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }

      static size_t slipEncode(const uint8_t *in, size_t inLen, uint8_t *out,
                               size_t outCap) {
        size_t o = 0;
        auto push = [&](uint8_t b) {
          if (o < outCap)
            out[o++] = b;
        };
        push(kSlipEnd);
        for (size_t i = 0; i < inLen; i++) {
          const uint8_t b = in[i];
          if (b == kSlipEnd) {
            push(kSlipEsc);
            push(kSlipEscEnd);
          } else if (b == kSlipEsc) {
            push(kSlipEsc);
            push(kSlipEscEsc);
          } else {
            push(b);
          }
        }
        push(kSlipEnd);
        return o;
      }

      static bool isValidSyncResponse(const uint8_t *pkt, size_t len) {
        // Response payload (after SLIP unescaping):
        // 0: direction (0x01)
        // 1: command (0x08)
        // 2-3: size (little endian)
        // 4-7: value (echo-like for SYNC in ROM)
        // 8..: data (size bytes), includes status bytes (ROM: 4 bytes at end)
        if (len < 8)
          return false;
        if (pkt[0] != 0x01)
          return false;
        if (pkt[1] != 0x08)
          return false;
        const uint16_t size = static_cast<uint16_t>(pkt[2]) | (static_cast<uint16_t>(pkt[3]) << 8);
        if (len < static_cast<size_t>(8 + size))
          return false;

        // The 4-byte "value" field is ROM-specific. Older traces often show
        // value=0x20120707 (bytes 07 07 12 20), but some ROMs respond
        // differently. We only require direction/command and a success status.

        // ROM loader status bytes are the last 4 bytes of the data field (and should be 0 on success).
        if (size < 4)
          return false;
        const size_t statusOff = 8 + static_cast<size_t>(size) - 4;
        if (!(pkt[statusOff + 0] == 0x00 && pkt[statusOff + 1] == 0x00 &&
              pkt[statusOff + 2] == 0x00 && pkt[statusOff + 3] == 0x00))
          return false;
        return true;
      }

      EsptoolBridge::EsptoolBridge(uint16_t tcpPort, int enGpio, int bootGpio)
          : EsptoolBridge([&]() {
              EsptoolBridgeConfig cfg;
              cfg.tcpPort = tcpPort;
              cfg.enGpio = enGpio;
              cfg.bootGpio = bootGpio;
              return cfg;
            }()) {
      }

      EsptoolBridge::EsptoolBridge(const EsptoolBridgeConfig &cfg) : _cfg(cfg) {
        _mutex = xSemaphoreCreateMutex();

        _physicalBaudrate = Uart::instance().defaults().baudrate;

        UartConfig uartCfg = _cfg.uartConfig;
        if (_cfg.applyUartConfig) {
          // Fill in unspecified pins from the current UART defaults.
          const auto base = Uart::instance().defaults();
          if (uartCfg.txGpio == UART_PIN_NO_CHANGE)
            uartCfg.txGpio = base.txGpio;
          if (uartCfg.rxGpio == UART_PIN_NO_CHANGE)
            uartCfg.rxGpio = base.rxGpio;
        }

        initPins();

        Rfc2217UartServerConfig scfg{};
        scfg.tcpPort = _cfg.tcpPort;
        scfg.ownerName = "esptool";
        scfg.taskStackSize = _cfg.taskStackSize;
        scfg.taskPriority = _cfg.taskPriority;
        scfg.taskCoreId = _cfg.taskCoreId;

        scfg.applyUartConfig = _cfg.applyUartConfig;
        scfg.uartConfig = uartCfg;

        scfg.baudrateCtx = this;
        scfg.onBaudrate = onBaudrateThunk;
        scfg.reportRequestedBaudrate = true;

        scfg.hooksCtx = this;
        scfg.onSessionStarted = onSessionStartedThunk;
        scfg.onSessionEnded = onSessionEndedThunk;

        scfg.controlCtx = this;
        scfg.onControl = onControlThunk;

        _server = std::make_unique<Rfc2217UartServer>(scfg);
        _server->start();

           logI("Esptool bridge cfg: port=%u bootGpio=%d enGpio=%d bootActiveLow=%d enActiveLow=%d swap=%d invDtr=%d invRts=%d ignoreCtl=%d ignoreCtlMs=%u holdBoot=%d", 
             static_cast<unsigned>(_cfg.tcpPort), _cfg.bootGpio, _cfg.enGpio,
             _cfg.bootActiveLow ? 1 : 0, _cfg.enActiveLow ? 1 : 0,
             _cfg.swapControlLines ? 1 : 0, _cfg.invertDtr ? 1 : 0,
             _cfg.invertRts ? 1 : 0, _cfg.ignoreClientControl ? 1 : 0,
             static_cast<unsigned>(_cfg.ignoreClientControlMs),
             _cfg.holdBootWhileSessionActive ? 1 : 0);
        logI("Esptool bridge listening on port %u", static_cast<unsigned>(_cfg.tcpPort));
      }

      unsigned EsptoolBridge::onBaudrateThunk(void *ctx, unsigned baudrate) {
        auto *self = static_cast<EsptoolBridge *>(ctx);
        return self ? self->onBaudrate(baudrate) : 0;
      }

      unsigned EsptoolBridge::onBaudrate(unsigned baudrate) {
        if (!baudrate)
          return 0;

        // Keep the physical UART at a sane speed; RFC2217 clients often request
        // 9600 during negotiation even though ESP ROM loader sync is 115200.
        const uint32_t minBr = _cfg.minPhysicalBaudrate ? _cfg.minPhysicalBaudrate : 115200;
        const uint32_t accepted = baudrate < minBr ? minBr : baudrate;

        if (accepted != baudrate)
          logI("RFC2217 baudrate requested %u, clamped to %u", baudrate, accepted);

        if (!_mutex)
          return accepted;
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return accepted;

        _physicalBaudrate = accepted;
        xSemaphoreGive(_mutex);
        return accepted;
      }

      void EsptoolBridge::initPins() {
        auto setupOut = [&](int pin, int initialLevel) {
          if (pin < 0)
            return;
          const auto gpio = static_cast<gpio_num_t>(pin);
          (void)gpio_reset_pin(gpio);
          (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
          (void)gpio_set_level(gpio, initialLevel);
        };

        // Deassert both lines by default.
        setupOut(_cfg.bootGpio, levelForAsserted(false, _cfg.bootActiveLow));
        setupOut(_cfg.enGpio, levelForAsserted(false, _cfg.enActiveLow));
      }

      void EsptoolBridge::setTargetLines(bool bootAsserted, bool enAsserted) {
        const int bootLevel = levelForAsserted(bootAsserted, _cfg.bootActiveLow);
        const int enLevel = levelForAsserted(enAsserted, _cfg.enActiveLow);

        if (_cfg.bootGpio >= 0)
          (void)gpio_set_level(static_cast<gpio_num_t>(_cfg.bootGpio), bootLevel);
        if (_cfg.enGpio >= 0)
          (void)gpio_set_level(static_cast<gpio_num_t>(_cfg.enGpio), enLevel);
      }

      void EsptoolBridge::applyControl(bool dtrAsserted, bool rtsAsserted) {
        // Interpret RFC2217 DTR/RTS as logical signal assertions. Some
        // auto-reset transistor networks are designed for USB-UART adapters
        // where the electrical DTR/RTS outputs are active-low; in that case we
        // may need to invert the RFC2217 semantics.
        const bool dtrEff = _cfg.invertDtr ? !dtrAsserted : dtrAsserted;
        const bool rtsEff = _cfg.invertRts ? !rtsAsserted : rtsAsserted;

        // Conventional mapping:
        // - DTR => BOOT
        // - RTS => EN (RESET)
        // Some circuits swap DTR/RTS roles.
        bool bootAsserted = _cfg.swapControlLines ? rtsEff : dtrEff;
        const bool enAsserted = _cfg.swapControlLines ? dtrEff : rtsEff;

        if (_cfg.holdBootWhileSessionActive) {
          bootAsserted = true;
        }

        setTargetLines(bootAsserted, enAsserted);

           const int bootLevel = levelForAsserted(bootAsserted, _cfg.bootActiveLow);
           const int enLevel = levelForAsserted(enAsserted, _cfg.enActiveLow);
           logD("applyControl: DTR=%d RTS=%d (eff DTR=%d RTS=%d) -> BOOT(pin=%d,level=%d) EN(pin=%d,level=%d)",
             dtrAsserted ? 1 : 0, rtsAsserted ? 1 : 0, dtrEff ? 1 : 0, rtsEff ? 1 : 0,
             _cfg.bootGpio, bootLevel, _cfg.enGpio, enLevel);
      }

      void EsptoolBridge::enterDownloadModePulse() {
        enterDownloadModePulse(_cfg.bootAssertLeadUs, _cfg.holdBootWhileSessionActive);
      }

      void EsptoolBridge::enterDownloadModePulse(uint32_t bootAssertLeadUs, bool keepBootAfterPulse) {
        if (_cfg.bootGpio < 0 || _cfg.enGpio < 0)
          return;

        logI("Entering download mode pulse");

        logI("pulse: idle (BOOT=deassert, EN=deassert)");
        taskENTER_CRITICAL(&s_gpioMux);
        setTargetLines(false, false);
        taskEXIT_CRITICAL(&s_gpioMux);
        delayMsWithWdt(20);

        logI("pulse: assert EN (reset)");
        taskENTER_CRITICAL(&s_gpioMux);
        setTargetLines(false, true);
        taskEXIT_CRITICAL(&s_gpioMux);
        delayMsWithWdt(_cfg.enterDownloadModeResetHoldMs);

        logI("pulse: assert BOOT before EN release (lead %u us)",
             static_cast<unsigned>(bootAssertLeadUs));
        taskENTER_CRITICAL(&s_gpioMux);
        setTargetLines(true, true);
        taskEXIT_CRITICAL(&s_gpioMux);
        if (bootAssertLeadUs) {
          feedWdt();
          esp_rom_delay_us(bootAssertLeadUs);
        }

        logI("pulse: release EN, keep BOOT asserted");
        taskENTER_CRITICAL(&s_gpioMux);
        setTargetLines(true, false);
        taskEXIT_CRITICAL(&s_gpioMux);
        delayMsWithWdt(_cfg.enterDownloadModeBootHoldMs);

        if (keepBootAfterPulse) {
          logI("Holding BOOT asserted after pulse");
          setTargetLines(true, false);
        } else {
          logI("pulse: release BOOT");
          setTargetLines(false, false);
        }

        if (_cfg.debugDumpUartAfterPulse) {
          const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(_cfg.debugDumpUartAfterPulseMs);
          uint8_t buf[96];
          size_t total = 0;
          size_t logged = 0;
          while (xTaskGetTickCount() < until) {
            feedWdt();
            const int n = Uart::instance().read(buf, sizeof(buf), pdMS_TO_TICKS(10));
            if (n > 0) {
              total += static_cast<size_t>(n);
              if (!logged) {
                const size_t toLog = (n > 64) ? 64 : static_cast<size_t>(n);
                logW("UART RX produced %u bytes right after pulse; first %u bytes:",
                     static_cast<unsigned>(total), static_cast<unsigned>(toLog));
                char hex[3 * 64 + 1];
                size_t o = 0;
                for (size_t i = 0; i < toLog && o + 3 <= sizeof(hex); i++) {
                  const int wrote = snprintf(hex + o, sizeof(hex) - o, "%02X ", buf[i]);
                  if (wrote <= 0)
                    break;
                  o += static_cast<size_t>(wrote);
                }
                if (o && o < sizeof(hex))
                  hex[o - 1] = 0;  // strip last space
                else
                  hex[sizeof(hex) - 1] = 0;
                logW("%s", hex);
                logged = toLog;
              }
            }
          }
          if (!total)
            logI("UART RX silent after pulse (good sign for ROM download mode)");
        }
      }

      void EsptoolBridge::drainUartRx(uint32_t maxTimeMs) {
        const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(maxTimeMs);
        uint8_t buf[128];
        while (xTaskGetTickCount() < until) {
          const int n = Uart::instance().read(buf, sizeof(buf), pdMS_TO_TICKS(10));
          if (n <= 0)
            break;
        }
      }

      bool EsptoolBridge::probeRomWithSync(uint32_t timeoutMs) {
        // Build a SYNC command packet as documented by Espressif.
        // Payload (after SLIP unescape):
        // 00 08 24 00 00 00 00 00  07 07 12 20  + 32x 55
        std::array<uint8_t, 8 + 36> payload{};
        payload[0] = 0x00;  // direction (request)
        payload[1] = 0x08;  // SYNC
        payload[2] = 0x24;
        payload[3] = 0x00;
        payload[4] = 0x00;
        payload[5] = 0x00;
        payload[6] = 0x00;
        payload[7] = 0x00;
        payload[8] = 0x07;
        payload[9] = 0x07;
        payload[10] = 0x12;
        payload[11] = 0x20;
        for (size_t i = 0; i < 32; i++)
          payload[12 + i] = 0x55;

        uint8_t wire[96];
        const size_t wireLen = slipEncode(payload.data(), payload.size(), wire, sizeof(wire));
        if (!wireLen) {
          logW("SYNC encode failed");
          return false;
        }

        // Clear any buffered bytes (e.g., app boot logs) before probing.
        drainUartRx(30);

        // Send SYNC a few times; esptool does multiple attempts and some
        // targets/links miss the first packet right after reset.
        for (int i = 0; i < 4; i++) {
          feedWdt();
          (void)Uart::instance().write(wire, wireLen);
          vTaskDelay(pdMS_TO_TICKS(10));
        }

        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeoutMs);
        bool inFrame = false;
        bool esc = false;
        uint8_t pkt[256];
        size_t pktLen = 0;

        while (xTaskGetTickCount() < deadline) {
          feedWdt();
          uint8_t b = 0;
          const int n = Uart::instance().read(&b, 1, pdMS_TO_TICKS(10));
          if (n <= 0)
            continue;

          if (b == kSlipEnd) {
            if (inFrame && pktLen > 0) {
              if (isValidSyncResponse(pkt, pktLen))
                return true;
            }
            inFrame = true;
            esc = false;
            pktLen = 0;
            continue;
          }

          if (!inFrame)
            continue;

          if (esc) {
            if (b == kSlipEscEnd)
              b = kSlipEnd;
            else if (b == kSlipEscEsc)
              b = kSlipEsc;
            esc = false;
          } else if (b == kSlipEsc) {
            esc = true;
            continue;
          }

          if (pktLen < sizeof(pkt))
            pkt[pktLen++] = b;
        }

        return false;
      }

      bool EsptoolBridge::autoTuneDownloadMode() {
        if (_cfg.bootGpio < 0 || _cfg.enGpio < 0)
          return false;

        const int64_t startUs = esp_timer_get_time();
        const uint32_t maxMs = _cfg.autoTuneMaxTimeMs ? _cfg.autoTuneMaxTimeMs : 1500;
        const uint32_t probeTimeout = _cfg.autoTuneProbeTimeoutMs ? _cfg.autoTuneProbeTimeoutMs : 200;
        const uint32_t gapMs = _cfg.autoTuneInterAttemptDelayMs;

        // Candidate lead times (us). We always try the configured value first.
        // Include longer values for RC/capacitor reset circuits (tens of ms).
        constexpr std::array<uint32_t, 18> defaults = {
          0, 100, 200, 500, 1000, 1500, 2000, 3000, 5000, 8000,
          12000, 16000, 24000, 32000, 50000, 80000, 120000, 200000};

        logI("Auto-tune: sweeping BOOT lead times, max %u ms", static_cast<unsigned>(maxMs));

        auto pulseForTune = [&](uint32_t leadUs) {
          const uint32_t resetHoldMs = _cfg.autoTuneResetHoldMs ? _cfg.autoTuneResetHoldMs : 80;
          const uint32_t bootHoldMs = _cfg.autoTuneBootHoldMs;

          // Fast pulse for tuning: no RX dump, short holds.
          taskENTER_CRITICAL(&s_gpioMux);
          setTargetLines(false, false);
          taskEXIT_CRITICAL(&s_gpioMux);
          delayMsWithWdt(10);

          taskENTER_CRITICAL(&s_gpioMux);
          setTargetLines(false, true);
          taskEXIT_CRITICAL(&s_gpioMux);
          delayMsWithWdt(resetHoldMs);

          taskENTER_CRITICAL(&s_gpioMux);
          setTargetLines(true, true);
          taskEXIT_CRITICAL(&s_gpioMux);
          if (leadUs) {
            feedWdt();
            esp_rom_delay_us(leadUs);
          }

          taskENTER_CRITICAL(&s_gpioMux);
          setTargetLines(true, false);
          taskEXIT_CRITICAL(&s_gpioMux);
          if (bootHoldMs)
            delayMsWithWdt(bootHoldMs);
        };

        auto tryLead = [&](uint32_t leadUs) -> bool {
          const int64_t elapsedMs = (esp_timer_get_time() - startUs) / 1000;
          if (elapsedMs >= static_cast<int64_t>(maxMs))
            return false;

          feedWdt();

          logI("Auto-tune: try bootAssertLeadUs=%u", static_cast<unsigned>(leadUs));

          // Keep BOOT asserted during probe to prevent accidental exits.
          pulseForTune(leadUs);

          if (_cfg.autoTunePostPulseDelayMs)
            vTaskDelay(pdMS_TO_TICKS(_cfg.autoTunePostPulseDelayMs));

          const bool ok = probeRomWithSync(probeTimeout);
          if (ok) {
            logI("Auto-tune: ROM SYNC OK at bootAssertLeadUs=%u", static_cast<unsigned>(leadUs));
            return true;
          }

          // Let target run normally between attempts.
          setTargetLines(false, false);
          if (gapMs)
            delayMsWithWdt(gapMs);
          return false;
        };

        // 1) Try configured value first.
        if (tryLead(_cfg.bootAssertLeadUs)) {
          if (_cfg.holdBootWhileSessionActive) {
            setTargetLines(true, false);
          } else {
            setTargetLines(false, false);
          }
          return true;
        }

        // 2) Try defaults, skipping duplicates.
        for (const uint32_t leadUs : defaults) {
          if (leadUs == _cfg.bootAssertLeadUs)
            continue;
          if (tryLead(leadUs)) {
            if (_cfg.holdBootWhileSessionActive) {
              setTargetLines(true, false);
            } else {
              setTargetLines(false, false);
            }
            return true;
          }
        }

        logW("Auto-tune: no ROM SYNC response found");
        return false;
      }

      void EsptoolBridge::onSessionStartedThunk(void *ctx) {
        auto *self = static_cast<EsptoolBridge *>(ctx);
        if (self)
          self->onSessionStarted();
      }

      void EsptoolBridge::onSessionEndedThunk(void *ctx) {
        auto *self = static_cast<EsptoolBridge *>(ctx);
        if (self)
          self->onSessionEnded();
      }

      void EsptoolBridge::onSessionStarted() {
        if (!_cfg.enterDownloadModeOnSessionStart)
          return;

        if (!_mutex)
          return;
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return;

        // Run a deterministic local pulse so the target enters ROM download
        // mode even if the client-side DTR/RTS toggling is imperfect.
        // Optionally auto-tune the BOOT lead time by probing ROM SYNC.
        if (_cfg.autoTuneDownloadModeOnSessionStart) {
          const bool tuned = autoTuneDownloadMode();
          if (!tuned) {
            logW("Auto-tune failed, falling back to fixed pulse");
            enterDownloadModePulse();
          }
        } else {
          enterDownloadModePulse();
        }

        if (_cfg.postPulseDelayMs) {
          logI("Post-pulse delay %u ms", static_cast<unsigned>(_cfg.postPulseDelayMs));
          vTaskDelay(pdMS_TO_TICKS(_cfg.postPulseDelayMs));
        }

        if (_cfg.ignoreClientControl || _cfg.ignoreClientControlMs) {
          _ignoreControlUntil = _cfg.ignoreClientControl ? portMAX_DELAY
                                                         : (xTaskGetTickCount() + pdMS_TO_TICKS(_cfg.ignoreClientControlMs));
        } else {
          _ignoreControlUntil = 0;
        }

        logI("Download mode pulse done");

        xSemaphoreGive(_mutex);
      }

      void EsptoolBridge::onSessionEnded() {
        if (!_mutex)
          return;
        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return;

        // Always release lines so the target boots normally after flashing.
        _dtr = false;
        _rts = false;
        _ignoreControlUntil = 0;
        setTargetLines(false, false);

        xSemaphoreGive(_mutex);
      }

      rfc2217_control_t EsptoolBridge::onControlThunk(void *ctx,
                                                     rfc2217_control_t requested) {
        auto *self = static_cast<EsptoolBridge *>(ctx);
        return self ? self->onControl(requested) : (rfc2217_control_t)-1;
      }

      rfc2217_control_t EsptoolBridge::onControl(rfc2217_control_t requested) {
        if (!_mutex)
          return (rfc2217_control_t)-1;

        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return (rfc2217_control_t)-1;

        switch (requested) {
          case RFC2217_CONTROL_SET_DTR:
            _dtr = true;
            break;
          case RFC2217_CONTROL_CLEAR_DTR:
            _dtr = false;
            break;
          case RFC2217_CONTROL_SET_RTS:
            _rts = true;
            break;
          case RFC2217_CONTROL_CLEAR_RTS:
            _rts = false;
            break;
          default:
            xSemaphoreGive(_mutex);
            return requested;
        }

        // Apply immediately so the reset-to-bootloader timing sequence is
        // preserved.
        const TickType_t now = xTaskGetTickCount();
        const bool ignore = (_cfg.ignoreClientControl) || (_ignoreControlUntil != 0 && now < _ignoreControlUntil);
        if (!ignore) {
          applyControl(_dtr, _rts);
          logD("RFC2217 control: DTR=%d RTS=%d", _dtr ? 1 : 0, _rts ? 1 : 0);
        } else {
          logD("RFC2217 control ignored: DTR=%d RTS=%d", _dtr ? 1 : 0, _rts ? 1 : 0);
        }

        xSemaphoreGive(_mutex);
        return requested;
      }

    }  // namespace term
  }  // namespace net
}  // namespace esp32m
