// T-Deck: RT-Thread's interrupt masking, as SF32TouchInput uses it (around a counter the touch
// "interrupt" bumps). Here that happens in the main loop too (TDeckTouch.cpp polls), so there's
// nothing to mask: kept for the shared code.
#pragma once
#include "rtthread.h"
static inline rt_base_t rt_hw_interrupt_disable(void) { return 0; }
static inline void rt_hw_interrupt_enable(rt_base_t) {}
