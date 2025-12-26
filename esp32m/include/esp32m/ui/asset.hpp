#pragma once

namespace esp32m {
  namespace ui {

    struct AssetInfo {
      const char* uri;
      const char* contentType;
      const char* contentEncoding;
      const char* etag;
    };

    enum class AssetType {
      Memory,
      Readable,
    };

    class Asset {
     public:
      virtual ~Asset() = default;
      virtual AssetType type() const = 0;
      const AssetInfo info() const {
        return _info;
      }

     protected:
      Asset() = default;
      void init(AssetInfo info) {
        this->_info = info;
      }

     private:
      AssetInfo _info = {};
    };

    class MemoryAsset : public Asset {
     public:
      MemoryAsset(AssetInfo& info, const uint8_t* buffer, size_t size)
          : _buffer(buffer), _size(size) {
        this->init(info);
      }

      AssetType type() const override {
        return AssetType::Memory;
      }

      const uint8_t* data() const {
        return _buffer;
      }
      size_t size() const {
        return _size;
      }

     private:
      const uint8_t* _buffer;
      const size_t _size;
    };

    class ReadableAsset : public Asset {
     public:
      AssetType type() const override {
        return AssetType::Readable;
      }

      virtual std::unique_ptr<stream::Reader> createReader() const = 0;
    };

  }  // namespace ui
}  // namespace esp32m
