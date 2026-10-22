#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "esp32m/dev/flmvalve/protocol.hpp"

namespace p = esp32m::dev::flmvalve::protocol;

template <size_t N>
void expectFrame(const p::Frame& actual,
                 const std::array<uint8_t, N>& expected) {
  assert(actual.size == expected.size());
  for (size_t i = 0; i < expected.size(); ++i)
    assert(actual.bytes[i] == expected[i]);
}

int main() {
  expectFrame(p::configureAddress(1, 3),
              std::array<uint8_t, 11>{0x01, 0x20, 0x00, 0x20, 0x00, 0x01,
                                      0x02, 0x00, 0x03, 0xe2, 0x25});
  expectFrame(p::configureAddress(3, 1),
              std::array<uint8_t, 11>{0x03, 0x20, 0x00, 0x20, 0x00, 0x01,
                                      0x02, 0x00, 0x01, 0x7a, 0x84});
  expectFrame(p::configureBaudrate(3, 9600),
              std::array<uint8_t, 13>{0x03, 0x30, 0x00, 0x30, 0x00, 0x01,
                                      0x04, 0x00, 0x00, 0x25, 0x80, 0xe1,
                                      0x6a});

  const std::array<uint8_t, 9> noisyAddressReply{
      0x55, 0xaa, 0x01, 0x20, 0x00, 0x01, 0x02, 0x92, 0x51};
  p::Reply reply;
  assert(p::findReply(noisyAddressReply.data(), noisyAddressReply.size(), 1,
                      p::ConfigureAddressFunction, 7, reply));
  assert(reply.offset == 2 && reply.size == 7);

  auto corrupt = noisyAddressReply;
  corrupt.back() ^= 1;
  assert(!p::findReply(corrupt.data(), corrupt.size(), 1,
                       p::ConfigureAddressFunction, 7, reply));

  std::cout << "PASS Flomarvel unicast configuration frames and reply parser\n";
}
