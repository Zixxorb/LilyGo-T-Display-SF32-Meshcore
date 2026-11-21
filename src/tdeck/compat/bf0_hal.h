// T-Deck: the bits of the SF32 HAL that SF32Display touches (see rtthread.h). The ESP32-S3's
// PSRAM is cache coherent for the CPU, and the panel is sent by the CPU: no cache upkeep.
#pragma once
#include <stdint.h>
#include "rtthread.h"

static inline void mpu_dcache_clean(void*, int) {}
static inline void mpu_dcache_invalidate(void*, int) {}

#define HAL_LCDC_LAYER_DEFAULT 0
struct LCDC_Layer { uint32_t total_width; };
typedef struct { LCDC_Layer Layer[1]; } LCDC_HandleTypeDef;
extern "C" LCDC_HandleTypeDef* get_drv_lcd_handler(void);
static inline void HAL_LCDC_Next_Frame_TE(LCDC_HandleTypeDef*, int) {}
static inline void HAL_LCDC_Exit_LP(LCDC_HandleTypeDef*) {}
// a panel register write: (0x02 << 24) | (reg << 8)
void HAL_LCDC_WriteU32Reg(LCDC_HandleTypeDef* h, uint32_t cmd, uint8_t* data, int n);
