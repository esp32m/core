#pragma once

namespace esp32m {
  namespace ui {

    struct Asset {
      const char *_uri;
      const char *_contentType;
      const uint8_t *_start;
      const uint8_t *_end;
      const char *_contentEncoding;
      const char *_etag;
      Asset(const char *uri, const char *contentType, const uint8_t *start,
            const uint8_t *end, const char *contentEncoding, const char *etag)
          : _uri(uri),
            _contentType(contentType),
            _start(start),
            _end(end),
            _contentEncoding(contentEncoding),
            _etag(etag) {}
    };

  }  // namespace ui
}  // namespace esp32m
