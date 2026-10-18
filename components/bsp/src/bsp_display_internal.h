#pragma once

#include "esp_err.h"

// Shared by the SPI transfer ceiling and LVGL draw buffer; keep them identical.
#define BSP_LCD_DMA_LINES 8

esp_err_t bsp_display_panel_wait_idle(void);
