#pragma once

#include <cstddef>
#include <cstdint>

using UBaseType_t = uint32_t;
using StackType_t = uint8_t;
void* pvPortMalloc(size_t);
void vPortFree(void*);
