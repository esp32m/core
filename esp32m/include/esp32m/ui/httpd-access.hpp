#pragma once

#include <stddef.h>
#include <string.h>

namespace esp32m {
  namespace ui {
    namespace http {

      // Request URIs include the query; registered endpoint names do not.
      inline size_t pathLength(const char* uri) {
        return uri ? strcspn(uri, "?") : 0;
      }

      inline bool pathMatches(const char* endpoint, const char* uri) {
        if (!endpoint || !uri)
          return false;
        const auto length = pathLength(uri);
        return strlen(endpoint) == length && !memcmp(endpoint, uri, length);
      }

      inline bool isWebSocketPath(const char* uri) {
        const auto length = pathLength(uri);
        return length >= 3 && !memcmp(uri, "/ws", 3) &&
               (length == 3 || uri[3] == '/');
      }

      inline bool uriMatches(const char* endpoint, const char* uri) {
        if (!endpoint || !uri)
          return false;
        if (!strcmp(endpoint, "/"))
          return !isWebSocketPath(uri);
        return pathMatches(endpoint, uri);
      }

      inline bool handlerMatches(const char* endpoint, int method,
                                 const char* uri, int requestMethod) {
        return method == requestMethod && pathMatches(endpoint, uri);
      }

      inline bool basicAuthorizationMatches(const char* expected,
                                            const char* received,
                                            size_t receivedLength) {
        if (!expected || !received || !*expected ||
            strlen(expected) != receivedLength)
          return false;
        unsigned char difference = 0;
        for (size_t i = 0; i < receivedLength; ++i)
          difference |= static_cast<unsigned char>(expected[i]) ^
                        static_cast<unsigned char>(received[i]);
        return difference == 0;
      }

    }  // namespace http
  }  // namespace ui
}  // namespace esp32m
