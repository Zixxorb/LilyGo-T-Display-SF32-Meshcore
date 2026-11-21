// T-Deck: the SF32's memory placement macros. The frame buffers etc. that the SF32 puts in its
// PSRAM section are allocated from the T-Deck's PSRAM instead (SF32Display.cpp, VESPER_TDECK).
#pragma once
#define ALIGN(n) __attribute__((aligned(n)))
#define L2_NON_RET_BSS_SECT_BEGIN(sec)
#define L2_NON_RET_BSS_SECT_END
#define L2_NON_RET_BSS_SECT(sec, decl) decl
