#pragma once

void testWarning(const char*, ...);
#define logw(...) testWarning(__VA_ARGS__)
