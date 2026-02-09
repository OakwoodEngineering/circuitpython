// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2021 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "supervisor/internal_flash.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifndef USE_MRAM_STORAGE
#define USE_MRAM_STORAGE 0
#endif

#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"
#if !USE_MRAM_STORAGE
#include "genhdr/flash_info.h"
#endif
#include "py/mphal.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "lib/oofatfs/ff.h"
#include "shared-bindings/microcontroller/__init__.h"

#include "audio_dma.h"
#include "supervisor/flash.h"
#include "supervisor/usb.h"

#ifdef PICO_RP2350
#include "hardware/structs/qmi.h"
#endif
#include "hardware/structs/sio.h"
#include "hardware/flash.h"
#include "pico/binary_info.h"
#include "pico/platform.h"

#if !defined(TOTAL_FLASH_MINIMUM)
#define TOTAL_FLASH_MINIMUM (2 * 1024 * 1024)
#endif

// TODO: Split the caching out of supervisor/shared/external_flash so we can use it.
#define SECTOR_SIZE 4096
#define NO_CACHE 0xffffffff
static uint8_t _cache[SECTOR_SIZE];
static uint32_t _cache_lba = NO_CACHE;
static uint32_t _flash_size = 0;
static bool _write_failed = false;
#if CIRCUITPY_AUDIOCORE
static uint32_t _audio_channel_mask;
#endif

#if USE_MRAM_STORAGE

#ifndef CIRCUITPY_MRAM_TOTAL_BYTES
#define CIRCUITPY_MRAM_TOTAL_BYTES (2 * 1024 * 1024)
#endif

#if (CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR % SECTOR_SIZE) != 0
#error "CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR must be 4KiB aligned for MRAM storage"
#endif

#if CIRCUITPY_MRAM_TOTAL_BYTES <= CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR
#error "CIRCUITPY_MRAM_TOTAL_BYTES must be larger than CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR"
#endif

#define MRAM_NVM_START_ADDR (CIRCUITPY_INTERNAL_NVM_START_ADDR - XIP_BASE)
#define MRAM_NVM_END_ADDR (MRAM_NVM_START_ADDR + CIRCUITPY_INTERNAL_NVM_SIZE)

#if MRAM_NVM_END_ADDR > CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR
#error "MRAM NVM region must be fully below CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR"
#endif

#define MRAM_PAGE_SIZE 256
#define MRAM_STATUS_BUSY_MASK 0x01
#define MRAM_STATUS_POLL_LIMIT 100000

#define MRAM_CMD_WRITE_ENABLE 0x06
#define MRAM_CMD_READ_STATUS 0x05
#define MRAM_CMD_PAGE_PROGRAM 0x02

static bool __no_inline_not_in_flash_func(mram_wait_ready)(void) {
    uint8_t txbuf[2] = {MRAM_CMD_READ_STATUS, 0xff};
    uint8_t rxbuf[2];
    for (size_t i = 0; i < MRAM_STATUS_POLL_LIMIT; i++) {
        flash_do_cmd(txbuf, rxbuf, sizeof(txbuf));
        if ((rxbuf[1] & MRAM_STATUS_BUSY_MASK) == 0) {
            return true;
        }
    }
    return false;
}

static bool __no_inline_not_in_flash_func(mram_write_enable)(void) {
    uint8_t txbuf[1] = {MRAM_CMD_WRITE_ENABLE};
    uint8_t rxbuf[1];
    flash_do_cmd(txbuf, rxbuf, sizeof(txbuf));
    return true;
}

static bool __no_inline_not_in_flash_func(mram_program_page)(uint32_t address, const uint8_t *data, size_t len) {
    if (len == 0 || len > MRAM_PAGE_SIZE) {
        return false;
    }

    uint8_t txbuf[4 + MRAM_PAGE_SIZE];
    uint8_t rxbuf[4 + MRAM_PAGE_SIZE];
    txbuf[0] = MRAM_CMD_PAGE_PROGRAM;
    txbuf[1] = (address >> 16) & 0xff;
    txbuf[2] = (address >> 8) & 0xff;
    txbuf[3] = address & 0xff;
    memcpy(txbuf + 4, data, len);
    flash_do_cmd(txbuf, rxbuf, len + 4);
    return true;
}

static bool __no_inline_not_in_flash_func(mram_write_bytes)(uint32_t address, const uint8_t *data, size_t len, bool allow_protected_range) {
    if (len == 0) {
        return true;
    }
    if (address + len < address || address + len > _flash_size) {
        return false;
    }
    if (!allow_protected_range && address < CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR) {
        return false;
    }

    while (len > 0) {
        size_t page_offset = address % MRAM_PAGE_SIZE;
        size_t chunk = MRAM_PAGE_SIZE - page_offset;
        if (chunk > len) {
            chunk = len;
        }

        if (!mram_wait_ready()) {
            return false;
        }
        if (!mram_write_enable()) {
            return false;
        }
        if (!mram_program_page(address, data, chunk)) {
            return false;
        }

        address += chunk;
        data += chunk;
        len -= chunk;
    }

    return mram_wait_ready();
}

bool supervisor_mram_write_nvm_bytes(uint32_t xip_address, const uint8_t *data, size_t len) {
    if (len == 0) {
        return true;
    }
    if (xip_address < XIP_BASE) {
        return false;
    }

    uint32_t linear_address = xip_address - XIP_BASE;
    if (linear_address < MRAM_NVM_START_ADDR ||
        linear_address + len < linear_address ||
        linear_address + len > MRAM_NVM_END_ADDR) {
        return false;
    }

    if (_flash_size == 0) {
        _flash_size = CIRCUITPY_MRAM_TOTAL_BYTES;
    }

    supervisor_flash_pre_write();
    bool ok = mram_write_bytes(linear_address, data, len, true);
    supervisor_flash_post_write();
    return ok;
}

#endif

void supervisor_flash_pre_write(void) {
    // Disable interrupts. XIP accesses will fault during flash writes.
    common_hal_mcu_disable_interrupts();
    #if CIRCUITPY_AUDIOCORE
    // Pause audio DMA to avoid noise while interrupts are disabled.
    _audio_channel_mask = audio_dma_pause_all();
    #endif
}

void supervisor_flash_post_write(void) {
    #if CIRCUITPY_AUDIOCORE
    // Unpause audio DMA.
    audio_dma_unpause_mask(_audio_channel_mask);
    #endif
    // Re-enable interrupts.
    common_hal_mcu_enable_interrupts();
}

void supervisor_flash_init(void) {
    bi_decl_if_func_used(bi_block_device(
        BINARY_INFO_MAKE_TAG('C', 'P'),
        "CircuitPython",
        CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR,
        TOTAL_FLASH_MINIMUM - CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR, // This is a minimum. We can't set it dynamically.
        NULL,
        BINARY_INFO_BLOCK_DEV_FLAG_READ |
        BINARY_INFO_BLOCK_DEV_FLAG_WRITE |
        BINARY_INFO_BLOCK_DEV_FLAG_PT_UNKNOWN));

    _write_failed = false;
    #if USE_MRAM_STORAGE
    _flash_size = CIRCUITPY_MRAM_TOTAL_BYTES;
    #else
    // Read the RDID register to get the flash capacity.
    uint8_t cmd[] = {0x9f, 0, 0, 0};
    uint8_t data[4];
    supervisor_flash_pre_write();
    flash_do_cmd(cmd, data, 4);
    supervisor_flash_post_write();
    uint8_t power_of_two = FLASH_DEFAULT_POWER_OF_TWO;
    // Flash must be at least 2MB (1 << 21) because we use the first 1MB for the
    // CircuitPython core. We validate the range because Adesto Tech flash chips
    // don't return the correct value. So, we default to 2MB which will work for
    // larger chips, it just won't use all of the space.
    if (data[3] >= 21 && data[3] < 30) {
        power_of_two = data[3];
    }
    _flash_size = 1 << power_of_two;
    #endif
}

uint32_t supervisor_flash_get_block_size(void) {
    return FILESYSTEM_BLOCK_SIZE;
}

uint32_t supervisor_flash_get_block_count(void) {
    return (_flash_size - CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR) / FILESYSTEM_BLOCK_SIZE;
}

void port_internal_flash_flush(void) {
    if (_cache_lba == NO_CACHE) {
        return;
    }
    #if USE_MRAM_STORAGE
    if (_write_failed) {
        return;
    }
    uint32_t write_addr = CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR + _cache_lba;
    supervisor_flash_pre_write();
    bool ok = mram_write_bytes(write_addr, _cache, SECTOR_SIZE, false);
    supervisor_flash_post_write();
    if (ok) {
        _cache_lba = NO_CACHE;
    } else {
        _write_failed = true;
    }
    #else
    supervisor_flash_pre_write();
    flash_range_erase(CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR + _cache_lba, SECTOR_SIZE);
    flash_range_program(CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR + _cache_lba, _cache, SECTOR_SIZE);
    _cache_lba = NO_CACHE;
    supervisor_flash_post_write();
    #endif
}

mp_uint_t supervisor_flash_read_blocks(uint8_t *dest, uint32_t block, uint32_t num_blocks) {
    if (_write_failed) {
        return 1;
    }
    if (block + num_blocks < block || block + num_blocks > supervisor_flash_get_block_count()) {
        return 1;
    }
    port_internal_flash_flush(); // we never read out of the cache, so we have to write it if dirty
    if (_write_failed) {
        return 1;
    }
    memcpy(dest,
        (void *)(XIP_BASE + CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR + block * FILESYSTEM_BLOCK_SIZE),
        num_blocks * FILESYSTEM_BLOCK_SIZE);
    return 0;
}

mp_uint_t supervisor_flash_write_blocks(const uint8_t *src, uint32_t lba, uint32_t num_blocks) {
    if (_write_failed) {
        return 1;
    }
    if (lba + num_blocks < lba || lba + num_blocks > supervisor_flash_get_block_count()) {
        return 1;
    }

    uint32_t blocks_per_sector = SECTOR_SIZE / FILESYSTEM_BLOCK_SIZE;
    uint32_t block = 0;
    while (block < num_blocks) {
        uint32_t block_address = lba + block;
        uint32_t sector_offset = block_address / blocks_per_sector * SECTOR_SIZE;
        uint8_t block_offset = block_address % blocks_per_sector;

        if (_cache_lba != sector_offset) {
            port_internal_flash_flush();
            if (_write_failed) {
                return 1;
            }
            memcpy(_cache,
                (void *)(XIP_BASE + CIRCUITPY_CIRCUITPY_DRIVE_START_ADDR + sector_offset),
                SECTOR_SIZE);
            _cache_lba = sector_offset;
        }
        for (uint8_t b = block_offset; b < blocks_per_sector; b++) {
            // Stop copying after the last block.
            if (block >= num_blocks) {
                break;
            }
            memcpy(_cache + b * FILESYSTEM_BLOCK_SIZE,
                src + block * FILESYSTEM_BLOCK_SIZE,
                FILESYSTEM_BLOCK_SIZE);
            block++;
        }
    }

    return _write_failed ? 1 : 0; // success
}

void supervisor_flash_release_cache(void) {
}
