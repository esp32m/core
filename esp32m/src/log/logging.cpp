#include "esp32m/logging.hpp"
#include "esp32m/base.hpp"
#include "esp32m/net/ota.hpp"

#include <esp32/rom/ets_sys.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <malloc.h>
#include <string.h>
#include <time.h>

namespace esp32m {
  namespace log {

    Level _level = Level::Debug;
    LogMessageFormatter _formatter = nullptr;
    LogAppender *_appenders = nullptr;
    SemaphoreHandle_t _loggingLock = xSemaphoreCreateMutex();

    const char DumpSubsitute = '.';

    inline char hdigit(int n) {
      return "0123456789abcdef"[n & 0xf];
    };

    const char *dumpline(char *dest, int bpl, const char *src,
                         const char *srcend) {
      if (src >= srcend)
        return 0;
      int i;
      unsigned long s = (unsigned long)src;
      for (i = 0; i < 8; i++) dest[i] = hdigit(s >> (28 - i * 4));

      dest[8] = ' ';
      dest += 9;
      for (i = 0; i < bpl; i++) {
        if (src + i < srcend) {
          dest[i * 3] = hdigit(src[i] >> 4);
          dest[i * 3 + 1] = hdigit(src[i]);
          dest[i * 3 + 2] = ' ';
          dest[bpl * 3 + i] =
              src[i] >= ' ' && src[i] < 0x7f ? src[i] : DumpSubsitute;
        } else {
          dest[i * 3] = dest[i * 3 + 1] = dest[i * 3 + 2] = dest[bpl * 3 + i] =
              ' ';
        }
      }
      return src + i;
    }

    LogMessage *LogMessage::alloc(Level level, int64_t stamp, const char *name,
                                  const char *message) {
      size_t ml = strlen(message);
      while (ml) {
        auto c = message[ml - 1];
        if (c == '\n' || c == '\r')
          ml--;
        else
          break;
      }
      size_t size = sizeof(LogMessage) + ml + 1;
      void *pool = malloc(size);
      return new (pool) LogMessage(size, level, stamp, name, message, ml);
    }

    LogMessage::LogMessage(size_t size, Level level, int64_t stamp,
                           const char *name, const char *message,
                           size_t messageLen)
        : _size(size), _stamp(stamp), _name(name), _level(level) {
      strncpy((char *)this->message(), message, messageLen)[messageLen] = '\0';
    }

    Logger &Loggable::logger() {
      if (_logger)
        return *_logger;
      xSemaphoreTake(_loggingLock, portMAX_DELAY);
      _logger = std::unique_ptr<Logger>(new Logger(*this));
      xSemaphoreGive(_loggingLock);
      return *_logger;
    }

    class BufferedAppender : public LogAppender {
     public:
      BufferedAppender(LogAppender &appender, size_t bufsize, bool autoRelease)
          : _appender(appender), _autoRelease(autoRelease) {
        _lock = xSemaphoreCreateMutex();
        _handle = xRingbufferCreate(bufsize, RINGBUF_TYPE_NOSPLIT);
      }
      ~BufferedAppender() {
        release();
      }

     protected:
      bool append(const LogMessage *message) {
        // can't use the buffer if it hasn't been created or if the task
        // scheduler is suspended
        if (!_handle || xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED)
          return _appender.append(message);
        size_t size;
        bool ok = true;
        LogMessage *item;
        while (_appender.append(nullptr)) {
          xSemaphoreTake(_lock, portMAX_DELAY);
          item = (LogMessage *)xRingbufferReceive(_handle, &size, 0);
          xSemaphoreGive(_lock);
          if (!item)
            break;
          ok &= _appender.append(item);
          vRingbufferReturnItem(_handle, item);
          xSemaphoreGive(_lock);
        }
        if (!_appender.append(message))
          for (;;) {
            xSemaphoreTake(_lock, portMAX_DELAY);
            if (xRingbufferSend(_handle, message, message->size(), 0)) {
              xSemaphoreGive(_lock);
              break;
            }
            item = (LogMessage *)xRingbufferReceive(_handle, &size, 0);
            xSemaphoreGive(_lock);
            if (!item)
              break;
            ok &= _appender.append(item);
            xSemaphoreTake(_lock, portMAX_DELAY);
            vRingbufferReturnItem(_handle, item);
            xSemaphoreGive(_lock);
          }
        if (ok && _autoRelease)
          release();
        return true;
      }

     private:
      LogAppender &_appender;
      bool _autoRelease;
      RingbufHandle_t _handle;
      SemaphoreHandle_t _lock;
      void release() {
        if (!_handle)
          return;
        xSemaphoreTake(_lock, portMAX_DELAY);
        vRingbufferDelete(_handle);
        xSemaphoreGive(_lock);
        _handle = nullptr;
        vSemaphoreDelete(_lock);
      }
    };

    class LogQueue;
    LogQueue *logQueue = nullptr;

    class LogQueue {
     public:
      LogQueue(size_t bufsize) : _bufsize(bufsize) {
        _lock = xSemaphoreCreateMutex();
        _buf = xRingbufferCreate(bufsize, RINGBUF_TYPE_NOSPLIT);
        xTaskCreate([](void *self) { ((LogQueue *)self)->run(); }, "m/logq",
                    4096, this, tskIDLE_PRIORITY, &_task);
        logQueue = this;
      }
      ~LogQueue() {
        xSemaphoreTake(_lock, portMAX_DELAY);
        vTaskDelete(_task);
        vRingbufferDelete(_buf);
        logQueue = nullptr;
        xSemaphoreGive(_lock);
        vSemaphoreDelete(_lock);
      }
      bool enqueue(const LogMessage *message) {
        xSemaphoreTake(_lock, portMAX_DELAY);
        auto result =
            xRingbufferSend(_buf, message, message->size(), pdMS_TO_TICKS(10));
        xSemaphoreGive(_lock);
        return result;
      }

     private:
      size_t _bufsize;
      SemaphoreHandle_t _lock;
      RingbufHandle_t _buf;
      TaskHandle_t _task = nullptr;
      void run() {
        esp_task_wdt_add(nullptr);
        for (;;) {
          esp_task_wdt_reset();
          size_t size;
          LogMessage *item =
              (LogMessage *)xRingbufferReceive(_buf, &size, pdMS_TO_TICKS(100));
          if (item) {
            LogAppender *appender = _appenders;
            while (appender) {
              appender->append(item);
              appender = appender->_next;
            }
            vRingbufferReturnItem(_buf, item);
          }
        }
      }
      friend void useQueue(int size);
    };

    int64_t timeOrUptime() {
      if (xPortCanYield()) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2016 - 1900))
          return -((int64_t)now * 1000 + (millis() % 1000));
      }
      return millis();
    }

    char *format(const LogMessage *msg) {
      static const char *levels = "??EWIDV";
      if (!msg)
        return nullptr;
      auto stamp = msg->stamp();
      char *buf = nullptr;
      auto level = msg->level();
      auto name = msg->name();
      char l = level >= 0 && level < 6 ? levels[level] : '?';
      if (stamp < 0) {
        stamp = -stamp;
        char strftime_buf[32];
        time_t now = stamp / 1000;
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%F %T", &timeinfo);
        buf = (char *)malloc(strlen(strftime_buf) + 1 /*dot*/ + 4 /*millis*/ +
                             1 /*space*/ + 1 /*level*/ + 1 /*space*/ +
                             strlen(name) + 2 /*spaces*/ + msg->message_size() +
                             1 /*zero*/);
        sprintf(buf, "%s.%04d %c %s  %s", strftime_buf, (int)(stamp % 1000), l,
                name, msg->message());
      } else {
        int millis = stamp % 1000;
        stamp /= 1000;
        int seconds = stamp % 60;
        stamp /= 60;
        int minutes = stamp % 60;
        stamp /= 60;
        int hours = stamp % 24;
        int days = stamp / 24;
        buf = (char *)malloc(
            6 /*days*/ + 1 /*colon*/ + 2 /*hours*/ + 1 /*colon*/ +
            2 /*minutes*/ + 1 /*colon*/ + 2 /*seconds*/ + 1 /*colon*/ +
            4 /*millis*/ + 1 /*space*/ + 1 /*level*/ + 1 /*space*/ +
            strlen(name) + 2 /*spaces*/ + msg->message_size() + 1 /*zero*/);
        sprintf(buf, "%d:%02d:%02d:%02d.%04d %c %s  %s", days, hours, minutes,
                seconds, millis, l, name, msg->message());
      }
      return buf;
    }

    FormattingAppender::FormattingAppender(LogMessageFormatter formatter) {
      _formatter = formatter == nullptr ? log::formatter() : formatter;
    }

    bool FormattingAppender::append(const LogMessage *message) {
      auto str = _formatter(message);
      if (!str)
        return true;
      auto result = this->append(str);
      free(str);
      return result;
    }

    bool isEmpty(const char *s) {
      if (!s)
        return true;
      auto l = strlen(s);
      if (!l)
        return true;
      for (int i = 0; i < l; i++)
        if (!isspace(s[i]))
          return false;
      return true;
    }

    void Logger::log(Level level, const char *msg) {
      if (isEmpty(msg))
        return;
      if (net::ota::isRunning())
        return;
      auto effectiveLevel = _level;
      if (effectiveLevel == Level::Default)
        effectiveLevel = log::level();
      if (level > effectiveLevel)
        return;
      auto name = _loggable.logName();
      LogMessage *message = LogMessage::alloc(level, timeOrUptime(), name, msg);
      if (!message)
        return;
      if (!_appenders) {
        auto m = formatter()(message);
        if (m) {
          ets_printf("%s\n", m);
          free(m);
        }
      } else {
        LogQueue *queue = logQueue;
        // we can't use queue if scheduler is suspended
        if (queue && xTaskGetSchedulerState() != taskSCHEDULER_SUSPENDED)
          queue->enqueue(message);
        else {
          LogAppender *appender = _appenders;
          while (appender) {
            appender->append(message);
            appender = appender->_next;
          }
        }
      }
      free(message);
    }

    void Logger::logf(Level level, const char *format, ...) {
      if (!format)
        return;
      va_list arg;
      va_start(arg, format);
      logf(level, format, arg);
      va_end(arg);
    }

    void Logger::logf(Level level, const char *format, va_list arg) {
      if (!format)
        return;
      if (net::ota::isRunning())
        return;
      char buf[64];
      char *temp = buf;
      auto len = vsnprintf(NULL, 0, format, arg);
      if (len >= sizeof(buf)) {
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
          return;
      }
      vsnprintf(temp, len + 1, format, arg);
      log(level, temp);
      if (len >= sizeof(buf))
        free(temp);
    }

    void Logger::dump(Level level, const void *buf, size_t buflen) {
      const int bpl = 16;
      const int destlen = 9 + 16 * 4;
      char dest[destlen + 1];
      dest[destlen] = 0;
      const char *start = (char *)buf;
      const char *cur = start;
      const char *end = start + buflen;
      while (!!(cur = dumpline(dest, bpl, cur, end))) log(level, dest);
    }

    void addBufferedAppender(LogAppender *a, int bufsize, bool autoRelease) {
      if (a)
        addAppender(new BufferedAppender(*a, bufsize, autoRelease));
    }

    void addAppender(LogAppender *a) {
      if (!a)
        return;
      LogAppender *appender = _appenders;
      if (!appender)
        _appenders = a;
      else {
        while (appender->_next) appender = appender->_next;
        appender->_next = a;
        a->_prev = appender;
      }
    }

    Level level() {
      return _level;
    }

    LogMessageFormatter formatter() {
      return _formatter == nullptr ? format : _formatter;
    }

    void setFormatter(LogMessageFormatter formatter) {
      _formatter = formatter;
    }

    void setLevel(Level level) {
      _level = level;
    }

    void removeAppender(LogAppender *a) {
      if (!a)
        return;
      LogAppender *appender = _appenders;
      if (appender == a)
        _appenders = nullptr;
      else
        while (appender) {
          if (appender == a) {
            if (a->_prev)
              a->_prev->_next = a->_next;
            if (a->_next)
              a->_next->_prev = a->_prev;
            break;
          }
          appender = appender->_next;
        }
    }

    void useQueue(int size) {
      auto q = logQueue;
      if (size) {
        if (q) {
          if (q->_bufsize == size)
            return;
          delete q;
        }
        new LogQueue(size);
      } else if (q)
        delete q;
    }

    Logger &system() {
      static SimpleLoggable loggable("system");
      return loggable.logger();
    }

    bool charToLevel(char c, Level &l) {
      switch (c) {
        case 'I':
          l = Level::Info;
          break;
        case 'W':
          l = Level::Warning;
          break;
        case 'D':
          l = Level::Debug;
          break;
        case 'E':
          l = Level::Error;
          break;
        case 'V':
          l = Level::Verbose;
          break;
        default:
          return false;
      }
      return true;
    }

    Level detectLevel(const char **mptr) {
      auto msg = *mptr;
      Level l = Level::None;
      if (msg) {
        auto len = strlen(msg);
        char lc = '\0';
        int inc = 0;
        if (len > 4 && msg[0] == '[' && msg[2] == ']') {
          lc = msg[1];
          inc = 3;
        } else if (len > 2 && msg[1] == ' ') {
          lc = msg[0];
          inc = 2;
        }
        if (charToLevel(lc, l))
          (*mptr) += inc;
      }
      if (!l)
        l = Level::Debug;
      return l;
    }

    class Esp32Hook;
    Esp32Hook *_esp32Hook = nullptr;

    class Esp32Hook {
     public:
      Esp32Hook() {
        _esp32Hook = this;
        _prevLogger = esp_log_set_vprintf(esp32hook);
      }
      ~Esp32Hook() {
        esp_log_set_vprintf(_prevLogger);
        _esp32Hook = nullptr;
      }

     private:
      vprintf_like_t _prevLogger = nullptr;
      uint8_t _recursion = 0;
      const char *_pendingName = nullptr;
      Level _pendingLevel = Level::None;
      static int esp32hook(const char *str, va_list arg) {
        auto h = _esp32Hook;
        if (!h || h->_recursion)
          return 0;
        h->_recursion++;
        if (h->_pendingName) {
          SimpleLoggable log(h->_pendingName);
          log.logger().logf(h->_pendingLevel, str, arg);
          h->_pendingLevel = Level::None;
          h->_pendingName = nullptr;
        } else if (!strcmp(str, "%c (%d) %s:")) {
          charToLevel((char)va_arg(arg, int), h->_pendingLevel);
          va_arg(arg, long);
          h->_pendingName = va_arg(arg, const char *);
        } else {
          const char *mptr = str;
          auto level = detectLevel(&mptr);
          system().logf(level, mptr, arg);
        }
        h->_recursion--;
        return strlen(str);
      }
    };

    void hookEsp32Logger(bool install) {
      auto h = _esp32Hook;
      if (install) {
        if (h)
          return;
        new Esp32Hook();
      } else {
        if (!h)
          return;
        delete h;
      }
    }

    class SerialHook;
    SerialHook *serialHook = nullptr;

    class SerialHook {
     public:
      SerialHook(size_t bufsize) {
        _lock = xSemaphoreCreateMutex();
        _serialBuf = (char *)malloc(_serialBufLen = bufsize);
        serialHook = this;
        ets_install_putc1(hook);
      }
      ~SerialHook() {
        ets_install_uart_printf();
        xSemaphoreTake(_lock, portMAX_DELAY);
        free(_serialBuf);
        _serialBufLen = 0;
        _serialBufPtr = 0;
        serialHook = nullptr;
        xSemaphoreGive(_lock);
        vSemaphoreDelete(_lock);
      }

     private:
      static void hook(char c) {
        auto h = serialHook;
        if (!h)
          return;
        if (h->_recursion) {
          ets_write_char_uart(c);
          return;
        }
        h->hookImpl(c);
      }
      void hookImpl(char c) {
        _recursion++;
        // if something prints to serial while we are in the critical section,
        // we must avoid context switches per esp-idf docs -
        // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html
        bool suspended = xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED;
        if (!suspended)
          xSemaphoreTake(_lock, portMAX_DELAY);
        auto b = _serialBuf;
        auto bl = _serialBufLen;
        if (b && bl) {
          if (c == '\n' || _serialBufPtr >= bl - 1) {
            b[_serialBufPtr] = 0;
            _serialBufPtr = 0;
            const char **mptr = (const char **)&b;
            auto level = detectLevel(mptr);
            if (!suspended)
              xSemaphoreGive(_lock);
            ets_install_uart_printf();
            system().log(level, *mptr);
            ets_install_putc1(hook);
            if (!suspended)
              xSemaphoreTake(_lock, portMAX_DELAY);
          }
          if (c != '\n' && c != '\r')
            b[_serialBufPtr++] = c;
        }
        if (!suspended)
          xSemaphoreGive(_lock);
        _recursion--;
      }

      SemaphoreHandle_t _lock;
      char *_serialBuf;
      size_t _serialBufLen;
      int _serialBufPtr = 0;
      uint8_t _recursion = 0;
      friend void hookUartLogger(int bufsize);
    };

    void hookUartLogger(int bufsize) {
      auto h = serialHook;
      if (bufsize) {
        if (h) {
          if (h->_serialBufLen == bufsize)
            return;
          delete h;
        }
        new SerialHook(bufsize);
      } else if (h)
        delete h;
    }
  }  // namespace log
}  // namespace esp32m