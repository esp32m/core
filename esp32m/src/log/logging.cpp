#include "esp32m/logging.hpp"
#include "esp32m/base.hpp"
#include "esp32m/net/ota.hpp"

#include <esp_rom_uart.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <malloc.h>
#include <rom/ets_sys.h>
#include <string.h>
#include <time.h>
#include <mutex>
#include <vector>
#include <algorithm> // needed for std::remove() to work

#include "sdkconfig.h"

namespace esp32m {
  namespace log {

#if CONFIG_ESP32M_LOG_LEVEL
    Level _level = (Level)CONFIG_ESP32M_LOG_LEVEL;
#else
    Level _level = Level::Debug;
#endif

    LogMessageFormatter _formatter = nullptr;
    std::vector<LogAppender *> _appenders;
    std::mutex _appendersLock;
    std::mutex _loggingLock;

    const char DumpSubsitute = '.';

    inline char hdigit(int n) {
      return "0123456789abcdef"[n & 0xf];
    };

    size_t bytes2hex(char *dest, size_t destSize, const uint8_t *src,
                     size_t srcSize) {
      size_t si, di = 0;
      for (si = 0; si < srcSize; si++) {
        if (destSize - di < 2)
          break;
        dest[di++] = hdigit(src[si] >> 4);
        dest[di++] = hdigit(src[si]);
        if (si == srcSize - 1)
          break;
        if (destSize - di < 1)
          break;
        dest[di++] = ' ';
      }
      if (destSize - di > 0)
        dest[di++] = 0;
      return di;
    }

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
      size_t nl = (name ? strlen(name) : 0) + 1;
      if (nl > 255)
        nl = 255;
      size_t ml = message ? strlen(message) : 0;
      while (ml) {
        auto c = message[ml - 1];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
          ml--;
        else
          break;
      }
      ml++;
      auto maxml = 65535 - (sizeof(LogMessage) + nl);
      if (ml > maxml)
        ml = maxml;
      const char *task = pcTaskGetName(xTaskGetCurrentTaskHandle());
      size_t tl = (task ? strlen(task) : 0) + 1;
      if (tl > 255)
        tl = 255;
      auto size = sizeof(LogMessage) + nl + ml + tl;
      void *pool = malloc(size);
      return new (pool)
          LogMessage(size, level, stamp, task, tl, name, nl, message, ml);
    }

    LogMessage::LogMessage(uint16_t size, Level level, int64_t stamp,
                           const char *task, uint8_t tasklen, const char *name,
                           uint8_t namelen, const char *message,
                           uint16_t messagelen)
        : _stamp(stamp),
          _size(size),
          _level(level),
          _namelen(namelen),
          _tasklen(tasklen) {
      strlcpy((char *)this->task(), task, tasklen);
      strlcpy((char *)this->name(), name, namelen);
      strlcpy((char *)this->message(), message, messagelen);
    }

    Logger &Loggable::logger() {
      if (!_logger) {
        std::lock_guard guard(_loggingLock);
        _logger = std::unique_ptr<Logger>(new Logger(*this));
      }
      if (_logger)
        return *_logger;
      // We get here if logger instance could not be created, most probably due
      // to low memory. Return system loger for this case
      return system();
    }

    class BufferedAppender : public LogAppender {
     public:
      BufferedAppender(LogAppender &appender, size_t bufsize, bool autoRelease)
          : _appender(appender), _autoRelease(autoRelease) {
        _handle = xRingbufferCreate(bufsize, RINGBUF_TYPE_NOSPLIT);
        _maxItemSize = _handle ? xRingbufferGetMaxItemSize(_handle) : 0;
      }
      ~BufferedAppender() {
        release();
      }

     protected:
      bool append(const LogMessage *message) {
        auto messageSize = message ? message->size() : 0;
        // this message can't be buffered if there's no buffer, or the size of
        // the buffer is less than the size of the message, of if the task
        // scheduler is suspended
        if (!_handle || _maxItemSize == 0 || _maxItemSize < messageSize ||
            xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED)
          return _appender.append(message);
        // null message means logger is asking whether this appender can append,
        // we answer yes if the buffer of any size is allocated
        if (!message)
          return true;
        size_t size;
        LogMessage *item;
        // try to add this message to the buffer
        for (;;) {
          if (xRingbufferSend(_handle, message, messageSize, 0))
            break;
          // buffer is full, take the oldest item, and either append or lose
          // it
          item = (LogMessage *)xRingbufferReceive(_handle, &size, 0);
          if (!item)
            // this should be impossible, but still better to check so we don't
            // try to call vRingbufferReturnItem() with null item
            break;
          _appender.append(
              item);  // we don't care whether it succeeded or not at this
                      // point, since there's no room in the buffer, so we're OK
                      // with dropping the oldest item in case of failure
          vRingbufferReturnItem(_handle, item);
          item = nullptr;
          // at this point some space has been freed in the buffer, so we try to
          // add message to the buffer again
        }
        // the buffer is consistent FIFO at this point, and we try to push as
        // many messages to the actual appender as possible
        for (;;) {
          item = (LogMessage *)xRingbufferReceive(_handle, &size, 0);
          if (!item) {
            // the buffer is empty
            if (_autoRelease)
              release();
            return true;
          }
          if (!_appender.append(item))
            // appender is not ready, we keep item in the buffer for later
            return true;
          vRingbufferReturnItem(_handle, item);
        }
      }

     private:
      LogAppender &_appender;
      bool _autoRelease;
      RingbufHandle_t _handle;
      size_t _maxItemSize;
      void release() {
        if (!_handle)
          return;
        vRingbufferDelete(_handle);
        _handle = nullptr;
      }
    };

    class LogQueue;
    LogQueue *logQueue = nullptr;

    class LogQueue {
     public:
      LogQueue(size_t bufsize) : _bufsize(bufsize) {
        _buf = xRingbufferCreate(bufsize, RINGBUF_TYPE_NOSPLIT);
        xTaskCreate([](void *self) { ((LogQueue *)self)->run(); }, "m/logq",
                    4096, this, tskIDLE_PRIORITY, &_task);
        logQueue = this;
      }
      ~LogQueue() {
        vTaskDelete(_task);
        vRingbufferDelete(_buf);
        logQueue = nullptr;
      }
      bool enqueue(const LogMessage *message) {
        return xRingbufferSend(_buf, message, message->size(), 0);
      }

     private:
      size_t _bufsize;
      RingbufHandle_t _buf;
      TaskHandle_t _task = nullptr;
      void run() {
        esp_task_wdt_add(nullptr);
        for (;;) {
          esp_task_wdt_reset();
          size_t size;
          LogMessage *item;
          item =
              (LogMessage *)xRingbufferReceive(_buf, &size, pdMS_TO_TICKS(100));
          if (item) {
            {
              std::lock_guard guard(_appendersLock);
              for (auto appender : _appenders) appender->append(item);
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
        gmtime_r(&now, &timeinfo);
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
      auto namelen = msg->namelen();
      auto taskname = msg->task();
      auto tasknamelen = msg->tasklen();
      auto messagelen = msg->messagelen();
      char l = level >= 0 && level <= 6 ? levels[level] : '?';
      if (stamp < 0) {
        stamp = -stamp;
        char strftime_buf[32];
        time_t now = stamp / 1000;
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%F %T", &timeinfo);
        auto buflen = strlen(strftime_buf) + 1 /*dot*/ + 3 /*millis*/ +
                      1 /*space*/ + 1 /*level*/ + 1 /*space*/ + 1 /*lbracket*/ +
                      tasknamelen + 2 /*rbracket space*/ + namelen +
                      2 /*spaces*/ + messagelen + 1 /*zero*/;
        buf = (char *)malloc(buflen);
        snprintf(buf, buflen, "%s.%03d %c [%s] %s  %s", strftime_buf,
                 (int)(stamp % 1000), l, taskname, name, msg->message());
      } else {
        int millis = stamp % 1000;
        stamp /= 1000;
        int seconds = stamp % 60;
        stamp /= 60;
        int minutes = stamp % 60;
        stamp /= 60;
        int hours = stamp % 24;
        int days = stamp / 24;
        auto buflen = 6 /*days*/ + 1 /*colon*/ + 2 /*hours*/ + 1 /*colon*/ +
                      2 /*minutes*/ + 1 /*colon*/ + 2 /*seconds*/ +
                      1 /*colon*/ + 3 /*millis*/ + 1 /*space*/ + 1 /*level*/ +
                      1 /*space*/ + 1 /*lbracket*/ + tasknamelen +
                      2 /*rbracket space*/ + namelen + 2 /*spaces*/ +
                      messagelen + 1 /*zero*/;
        buf = (char *)malloc(buflen);
        snprintf(buf, buflen, "%d:%02d:%02d:%02d.%03d %c [%s] %s  %s", days,
                 hours, minutes, seconds, millis, l, taskname, name,
                 msg->message());
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
      bool noAppenders;
      {
        std::lock_guard guard(_appendersLock);
        noAppenders = _appenders.size() == 0;
      }
      if (noAppenders) {
        auto m = formatter()(message);
        if (m) {
          ets_printf(m);
          ets_printf("\n");
          free(m);
        }
      } else {
        LogQueue *queue = logQueue;
        bool enqueued = false;
        // we can't use queue if scheduler is suspended
        if (queue && xTaskGetSchedulerState() != taskSCHEDULER_SUSPENDED)
          enqueued = queue->enqueue(message);
        if (!enqueued) {
          std::lock_guard guard(_appendersLock);
          for (auto appender : _appenders) {
            appender->append(message);
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
      va_list a2;
      va_copy(a2, arg); // spec says arg is undefined after vsnprintf() call, so need to make a copy
      auto len = vsnprintf(NULL, 0, format, arg);
      if (len >= sizeof(buf)) {
        temp = (char *)malloc(len + 1);
        if (temp == NULL) {
          va_end(a2);
          return;
        }
      }
      vsnprintf(temp, len + 1, format, a2);
      va_end(a2);
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
      std::lock_guard guard(_appendersLock);
      for (auto appender : _appenders)
        if (appender == a)
          return;
      _appenders.push_back(a);
    }

    Level level() {
      if (_level == Level::Default) {
#if CONFIG_LOG_DEFAULT_LEVEL > 0
        return (Level)(CONFIG_LOG_DEFAULT_LEVEL + 1);
#endif
        return Level::Debug;
      }
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

    bool hasAppenders() {
      std::lock_guard guard(_appendersLock);
      return _appenders.size() != 0;
    }

    void removeAppender(LogAppender *a) {
      if (!a)
        return;
      std::lock_guard guard(_appendersLock);
      _appenders.erase(std::remove(_appenders.begin(), _appenders.end(), a),
                       _appenders.end());
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
        if (len > 3 && msg[0] == 0x1b && msg[1] == '[') {
          // color sequence is "\u00x1b[X;XXm", so skip past 'm'
          int p = 3;
          while (p < len && p < 10)
            if (msg[p++] == 'm') {
              msg += p;
              len -= p;
              break;
            }
        }
        if (len > 4 && msg[0] == '[' && msg[2] == ']') {
          lc = msg[1];
          msg += 3;
        } else if (len > 2 && msg[1] == ' ') {
          lc = msg[0];
          msg += 2;
        }
        if (charToLevel(lc, l))
          *mptr = msg;
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
        _serialBuf = (char *)malloc(_serialBufLen = bufsize);
        serialHook = this;
//        ets_install_putc1(hook);
        esp_rom_install_channel_putc(1, hook);
      }
      ~SerialHook() {
        ets_install_uart_printf();
        std::lock_guard guard(_lock);
        free(_serialBuf);
        _serialBufLen = 0;
        _serialBufPtr = 0;
        serialHook = nullptr;
      }

     private:
      static void hook(char c) {
        auto h = serialHook;
        if (!h)
          return;
        if (h->_recursion) {
//          ets_write_char_uart(c);
          esp_rom_output_putc(c);
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
          _lock.lock();
        auto b = _serialBuf;
        auto bl = _serialBufLen;
        if (b && bl) {
          if (c == '\n' || _serialBufPtr >= bl - 1) {
            b[_serialBufPtr] = 0;
            _serialBufPtr = 0;
            const char **mptr = (const char **)&b;
            auto level = detectLevel(mptr);
            if (!suspended)
              _lock.unlock();
            ets_install_uart_printf();
            system().log(level, *mptr);
            ets_install_putc1(hook);
            if (!suspended)
              _lock.lock();
          }
          if (c != '\n' && c != '\r')
            b[_serialBufPtr++] = c;
        }
        if (!suspended)
          _lock.unlock();
        _recursion--;
      }

      std::mutex _lock;
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