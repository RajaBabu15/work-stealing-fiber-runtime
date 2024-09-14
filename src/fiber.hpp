#pragma once
#include <cstddef>
#include <cstdint>

extern "C" void fiber_switch(void** from_sp, void* to_sp);
