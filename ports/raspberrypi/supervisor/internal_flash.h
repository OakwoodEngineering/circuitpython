// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2021 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mpconfigport.h"

#ifndef USE_MRAM_STORAGE
#define USE_MRAM_STORAGE 0
#endif

// These must be called before and after doing a low-level flash write.
void supervisor_flash_pre_write(void);
void supervisor_flash_post_write(void);

#if USE_MRAM_STORAGE
bool supervisor_mram_write_nvm_bytes(uint32_t xip_address, const uint8_t *data, size_t len);
#endif

// #define INTERNAL_FLASH_PART1_NUM_BLOCKS (CIRCUITPY_INTERNAL_FLASH_FILESYSTEM_SIZE / FILESYSTEM_BLOCK_SIZE)

// #define INTERNAL_FLASH_SYSTICK_MASK    (0x1ff) // 512ms
// #define INTERNAL_FLASH_IDLE_TICK(tick) (((tick) & INTERNAL_FLASH_SYSTICK_MASK) == 2)
