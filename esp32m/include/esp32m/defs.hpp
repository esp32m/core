#pragma once

#define ESP_CHECK_RETURN(x)                              \
  do {                                                   \
    esp_err_t __;                                        \
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(__ = x) != ESP_OK) \
      return __;                                         \
  } while (0)
#define ESP_CHECK_RETURN_BOOL(x)                  \
  if (ESP_ERROR_CHECK_WITHOUT_ABORT(x) != ESP_OK) \
  return false
#define ESP_CHECK_LOGW_RETURN(x, msg, ...) \
  do {                                     \
    esp_err_t __;                          \
    if ((__ = x) != ESP_OK) {              \
      logW(msg, ##__VA_ARGS__);            \
      return __;                           \
    }                                      \
  } while (0)
#define ENUM_FLAG_OPERATORS(T)                                             \
  inline T operator~(T a) {                                                \
    return static_cast<T>(~static_cast<std::underlying_type<T>::type>(a)); \
  }                                                                        \
  inline T operator|(T a, T b) {                                           \
    return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) |  \
                          static_cast<std::underlying_type<T>::type>(b));  \
  }                                                                        \
  inline T operator&(T a, T b) {                                           \
    return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) &  \
                          static_cast<std::underlying_type<T>::type>(b));  \
  }                                                                        \
  inline T operator^(T a, T b) {                                           \
    return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) ^  \
                          static_cast<std::underlying_type<T>::type>(b));  \
  }                                                                        \
  inline T& operator|=(T& a, T b) {                                        \
    return reinterpret_cast<T&>(                                           \
        reinterpret_cast<std::underlying_type<T>::type&>(a) |=             \
        static_cast<std::underlying_type<T>::type>(b));                    \
  }                                                                        \
  inline T& operator&=(T& a, T b) {                                        \
    return reinterpret_cast<T&>(                                           \
        reinterpret_cast<std::underlying_type<T>::type&>(a) &=             \
        static_cast<std::underlying_type<T>::type>(b));                    \
  }                                                                        \
  inline T& operator^=(T& a, T b) {                                        \
    return reinterpret_cast<T&>(                                           \
        reinterpret_cast<std::underlying_type<T>::type&>(a) ^=             \
        static_cast<std::underlying_type<T>::type>(b));                    \
  }                                                                        \
  inline bool operator==(T a, int b) {                                     \
    return static_cast<std::underlying_type<T>::type>(a) == b;             \
  }                                                                        \
  inline bool operator!=(T a, int b) {                                     \
    return static_cast<std::underlying_type<T>::type>(a) != b;             \
  }
