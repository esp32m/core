#include "esp32m/dev/bfs/parameters.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace esp32m::dev::bfs;

static const Parameter& parameter(uint16_t address) {
  const auto* result = findParameter(address);
  assert(result);
  return *result;
}

static void allowlist() {
  // Independent group lengths from API v1.0: 82 rows, three comm exclusions.
  constexpr unsigned groupCounts[] = {23, 12, 16, 13, 18};
  size_t found = 0;
  for (uint16_t group = 0; group < 5; ++group)
    for (uint16_t index = 0; index < groupCounts[group]; ++index) {
      const uint16_t address = (group << 8) | index;
      const auto* item = findParameter(address);
      if (group == 3 && index >= 7 && index <= 9) {
        assert(!item);
        continue;
      }
      assert(item);
      char id[8];
      std::snprintf(id, sizeof(id), "F%u-%02u", group, index);
      assert(std::strcmp(item->id, id) == 0);
      assert(item->name && *item->name && item->group && *item->group);
      assert(item->min <= item->max);
      if (!item->writable)
        assert(item->reason && *item->reason);
      ++found;
    }
  assert(found == 79 && parametersCount == found + 1);
  assert(std::strcmp(parameter(0x0017).id, "0x0017") == 0);
  assert(parameter(0x0017).writable && parameter(0x0017).scale == .1);
  for (size_t i = 1; i < parametersCount; ++i)
    assert(Parameters[i - 1].address < Parameters[i].address);
  for (const uint16_t address : {0x0018, 0x0022, 0x010C, 0x020F + 1,
                                 0x030D, 0x0412, 0x0F00, 0x1008,
                                 0x2000, 0x2001, 0x2002, 0xFFFF})
    assert(!findParameter(address));
  assert(std::strcmp(parameter(0x0016).id, "F0-22") == 0);
  assert(std::strcmp(parameter(0x010A).id, "F1-10") == 0);
  assert(std::strcmp(parameter(0x030C).id, "F3-12") == 0);
}

static void readOnlyBoundaries() {
  constexpr uint16_t blocked[] = {
      0x000C, 0x000D, 0x0105, 0x0106, 0x0107, 0x0200, 0x0201,
      0x0202, 0x0203, 0x0205, 0x0208, 0x030C, 0x0401,
      0x0403, 0x0406, 0x0408, 0x040B, 0x040E, 0x0410, 0x0411};
  for (auto address : blocked)
    for (uint32_t raw = 0; raw <= 0xFFFF; ++raw)
      assert(!validateRaw(parameter(address), uint16_t(raw)));
  for (auto address : {0x0202, 0x0203, 0x0208})
    assert(parameter(address).scale == 0);
  for (auto address : {0x0401, 0x0403, 0x0406, 0x0408}) {
    const auto& item = parameter(address);
    assert(item.inferred && item.signedValue);
    assert(item.min == -1000 && item.max == 1000);
  }
  for (const auto& item : Parameters) {
    assert(item.stopRequired == (item.address != 0x0017));
    assert(item.persistenceKnown == (item.address == 0x0105));
  }
}

static void documentedRanges() {
  struct Bounds {
    uint16_t address, min, max;
  };
  constexpr Bounds bounds[] = {
      {0x0002, 1, 600}, {0x0003, 0, 100}, {0x0004, 10, 600},
      {0x0009, 1, 99}, {0x000A, 1, 99}, {0x0010, 0, 1000},
      {0x0012, 0, 1000}, {0x0015, 0, 9999}, {0x0016, 0, 9999},
      {0x0101, 20, 200}, {0x0102, 1, 600}, {0x0103, 1, 600},
      {0x0104, 0, 30000}, {0x0108, 0, 255}, {0x0109, 0, 1000},
      {0x010A, 0, 30000}, {0x010B, 0, 30000},
      {0x0204, 0, 3}, {0x0206, 30, 120}, {0x0207, 0, 255}, {0x0209, 0, 255},
      {0x020A, 0, 1000}, {0x020B, 0, 65535}, {0x020C, 0, 1000},
      {0x0301, 1, 1000}, {0x0302, 1, 1000}, {0x0303, 100, 30000},
      {0x0304, 1, 16}, {0x0305, 0, 255}, {0x030A, 0, 200},
      {0x030B, 1, 1000}, {0x0400, 0, 500}, {0x0402, 0, 500},
      {0x0404, 1, 50}, {0x0405, 0, 2000}, {0x0407, 0, 2000},
      {0x0409, 1, 50}, {0x040C, 0, 1000}, {0x040F, 0, 2500}};
  for (const auto& bounds : bounds) {
    const auto& item = parameter(bounds.address);
    assert(validateRaw(item, bounds.min));
    assert(validateRaw(item, bounds.max));
    if (bounds.min)
      assert(!validateRaw(item, bounds.min - 1));
    if (bounds.max < 65535)
      assert(!validateRaw(item, bounds.max + 1));
  }
  const auto ramp = parameter(0x0102).scale;
  assert(std::abs(75 * ramp - 7.5) < 1e-9);
  assert(std::abs(54 * parameter(0x0105).scale - 5.4) < 1e-9);
  assert(std::abs(20 * parameter(0x0016).scale - .2) < 1e-9);
  assert(std::abs(10 * parameter(0x040C).scale - .010) < 1e-9);
}

static void enumerations() {
  const auto& phaseLoss = parameter(0x0204);
  assert(phaseLoss.scale == 1 && !phaseLoss.unit && !phaseLoss.optionCount);
  for (uint32_t raw = 0; raw <= 0xFFFF; ++raw)
    assert(validateRaw(phaseLoss, uint16_t(raw)) == (raw <= 3));
  assert(validateRaw(parameter(0x000F), 2));
  assert(!validateRaw(parameter(0x000F), 3));
  assert(validateRaw(parameter(0x0306), 1));
  assert(!validateRaw(parameter(0x0306), 2));
  assert(validateRaw(parameter(0x040A), 6));
  assert(!validateRaw(parameter(0x040A), 7));
  for (const auto& item : Parameters) {
    if (!item.optionCount || !item.writable)
      continue;
    assert(item.options);
    for (size_t i = 0; i < item.optionCount; ++i) {
      assert(item.options[i].label && *item.options[i].label);
      assert(validateRaw(item, item.options[i].value));
    }
  }
}

static void protectionWords() {
  size_t protectionCount = 0, limitCount = 0;
  for (uint32_t raw = 0; raw <= 65535; ++raw) {
    // Independent bit mask: one bit for each on/off flag and two timeout bits.
    const bool protectionExpected = (raw & ~0x1131u) == 0;
    assert(validateRaw(parameter(0x020D), uint16_t(raw)) ==
           protectionExpected);
    const bool limitExpected = (raw & ~0x0111u) == 0;
    assert(validateRaw(parameter(0x020E), uint16_t(raw)) == limitExpected);
    protectionCount += protectionExpected;
    limitCount += limitExpected;
  }
  assert(protectionCount == 32 && limitCount == 8);
  assert(validateRaw(parameter(0x020D), 0x1101));
  assert(validateRaw(parameter(0x020D), 0x1111));
  assert(validateRaw(parameter(0x020D), 0x1131));
  assert(!validateRaw(parameter(0x020D), 0x0020 + 2));
  assert(!validateRaw(parameter(0x020D), 0x0102));
  assert(!validateRaw(parameter(0x020E), 0x0012));
  assert(!validateRaw(parameter(0x020E), 0x1000));
}

int main() {
  allowlist();
  readOnlyBoundaries();
  documentedRanges();
  enumerations();
  protectionWords();
  std::puts("BFS parameter tests: 5 scenario groups passed");
}
