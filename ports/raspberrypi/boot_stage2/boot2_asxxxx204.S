// ----------------------------------------------------------------------------
// Second stage boot code
// Copyright (c) 2026 Raspberry Pi (Trading) Ltd.
// SPDX-License-Identifier: BSD-3-Clause
//
// Device:      Avalanche ASxxxx204 SPI MRAM family
//              (1Mbit/4Mbit/8Mbit/16Mbit)
//
// Description: Configures ASxxxx204 to run in Quad I/O XIP mode using QMI
//
// Details:     * Program CR2 latency bits to match boot2 dummy cycles.
//              * Use QMI to perform a dummy 0xEB read with XIP mode byte Axh.
//              * Reconfigure QMI to omit command prefix for subsequent reads.
//              * Return through standard boot2 exit path.
//
// Notes:       * CR2 persists across power loss. This quad mode prevents ROM
//                boot on the tested ASxxxx204 while latency remains nonzero.
//                Use boot2_asxxxx204_03h.S for normal standalone boot.
//              * Uses QMI CS0 only (QSPI_SS pin).
//              * Does not touch QMI window 1 (e.g. PSRAM on CS1 / GPIO0).
//
// Building:    * This code must be position-independent, and use stack only
//              * The code will be padded to a size of 256 bytes, including a
//                4-byte checksum. Therefore code size cannot exceed 252 bytes.
// ----------------------------------------------------------------------------

#include "pico/asm_helper.S"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pads_qspi.h"
#include "hardware/regs/qmi.h"

// ----------------------------------------------------------------------------
// Config section
// ----------------------------------------------------------------------------

// The serial interface will run at clk_sys/PICO_FLASH_SPI_CLKDIV.
// This must be a positive integer.
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
#if (PICO_FLASH_SPI_CLKDIV << QMI_M0_TIMING_CLKDIV_LSB) & ~QMI_M0_TIMING_CLKDIV_BITS
#error "CLKDIV greater than maximum"
#endif

// RX sampling delay is measured in units of one half clock cycle.
#ifndef PICO_FLASH_SPI_RXDELAY
#define PICO_FLASH_SPI_RXDELAY 2
#endif
#if (PICO_FLASH_SPI_RXDELAY << QMI_M0_TIMING_RXDELAY_LSB) & ~QMI_M0_TIMING_RXDELAY_BITS
#error "RX delay greater than maximum"
#endif

// ASxxxx204 RDQI command (1-4-4 SDR) and XIP mode byte.
#define CMD_READ 0xeb
#define MODE_CONTINUOUS_READ 0xa0

// ASxxxx204 datasheet defines XIP latency cycles in the range 8..15.
#ifndef PICO_MRAM_XIP_READ_LATENCY
#define PICO_MRAM_XIP_READ_LATENCY 8
#endif
#if PICO_MRAM_XIP_READ_LATENCY < 8 || PICO_MRAM_XIP_READ_LATENCY > 15
#error "PICO_MRAM_XIP_READ_LATENCY must be between 8 and 15 cycles"
#endif
// QMI dummy length is encoded in units of 4 bits, so with single-width dummy
// phase only cycle counts divisible by 4 are representable.
#if (PICO_MRAM_XIP_READ_LATENCY & 0x3)
#error "PICO_MRAM_XIP_READ_LATENCY must be a multiple of 4 (e.g. 8 or 12)"
#endif
#define WAIT_CYCLES (PICO_MRAM_XIP_READ_LATENCY / 4)
#if (WAIT_CYCLES << QMI_M0_RFMT_DUMMY_LEN_LSB) & ~QMI_M0_RFMT_DUMMY_LEN_BITS
#error "Latency greater than maximum supported by QMI_M0_RFMT_DUMMY_LEN"
#endif

// Program CR2 latency so the device expectation matches WAIT_CYCLES.
#define PROGRAM_CR2_LATENCY
#define CMD_READ_CR2 0x3f
#define CMD_WRITE_ENABLE 0x06
#define CMD_WRITE_ANY_REGISTER 0x71
#define REG_ADDR_CR2 0x03
#define CR2_VALUE PICO_MRAM_XIP_READ_LATENCY

// ----------------------------------------------------------------------------
// Register initialisation values -- same in Arm/RISC-V code.
// ----------------------------------------------------------------------------

// SCLK: 8mA drive, no slew limiting, input buffer disabled
#define INIT_PAD_SCLK  (\
    2 << PADS_QSPI_GPIO_QSPI_SCLK_DRIVE_LSB | \
    PADS_QSPI_GPIO_QSPI_SCLK_SLEWFAST_BITS | \
0)

// Use direct serial mode for one-time control register writes.
// Keep this conservative and use CS0 only.
#define INIT_DIRECT_CSR (\
    30 << QMI_DIRECT_CSR_CLKDIV_LSB | \
    QMI_DIRECT_CSR_EN_BITS | \
    QMI_DIRECT_CSR_AUTO_CS0N_BITS | \
0)

// Setup timing parameters: short sequential-access cooldown, configured
// CLKDIV and RXDELAY. Add a CS-high recovery interval for MRAM reads.
#define INIT_M0_TIMING (\
    1                      << QMI_M0_TIMING_COOLDOWN_LSB |\
    3                      << QMI_M0_TIMING_MIN_DESELECT_LSB |\
    PICO_FLASH_SPI_RXDELAY << QMI_M0_TIMING_RXDELAY_LSB |\
    PICO_FLASH_SPI_CLKDIV  << QMI_M0_TIMING_CLKDIV_LSB |\
0)

// Set command constants (EBh + Axh)
#define INIT_M0_RCMD (\
    CMD_READ             << QMI_M0_RCMD_PREFIX_LSB |\
    MODE_CONTINUOUS_READ << QMI_M0_RCMD_SUFFIX_LSB |\
0)

// Set read format:
// - command prefix, address, suffix, and data on quad lines
// - latency on single line so cycle count maps directly to dummy bits
#define INIT_M0_RFMT (\
    QMI_M0_RFMT_PREFIX_WIDTH_VALUE_S << QMI_M0_RFMT_PREFIX_WIDTH_LSB |\
    QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_RFMT_ADDR_WIDTH_LSB |\
    QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |\
    QMI_M0_RFMT_DUMMY_WIDTH_VALUE_S  << QMI_M0_RFMT_DUMMY_WIDTH_LSB |\
    QMI_M0_RFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_RFMT_DATA_WIDTH_LSB |\
    QMI_M0_RFMT_PREFIX_LEN_VALUE_8   << QMI_M0_RFMT_PREFIX_LEN_LSB |\
    QMI_M0_RFMT_SUFFIX_LEN_VALUE_8   << QMI_M0_RFMT_SUFFIX_LEN_LSB |\
    WAIT_CYCLES                      << QMI_M0_RFMT_DUMMY_LEN_LSB |\
0)

// ----------------------------------------------------------------------------
// Start of 2nd Stage Boot Code
// ----------------------------------------------------------------------------

pico_default_asm_setup
.section .text

// ----------------------------------------------------------------------------
// RISC-V implementation

#ifdef __riscv

// On RP2350 boot stage2 is always called as a regular function, and should return normally
regular_func _stage2_boot
    mv t0, ra
_pad_config:
    li a3, PADS_QSPI_BASE
    li a0, INIT_PAD_SCLK
    sw a0, PADS_QSPI_GPIO_QSPI_SCLK_OFFSET(a3)
    // SDx: disable input Schmitt to reduce delay
    li a3, PADS_QSPI_BASE + REG_ALIAS_CLR_BITS
    li a0, PADS_QSPI_GPIO_QSPI_SD0_SCHMITT_BITS
    sw a0, PADS_QSPI_GPIO_QSPI_SD0_OFFSET(a3)
    sw a0, PADS_QSPI_GPIO_QSPI_SD1_OFFSET(a3)
    sw a0, PADS_QSPI_GPIO_QSPI_SD2_OFFSET(a3)
    sw a0, PADS_QSPI_GPIO_QSPI_SD3_OFFSET(a3)

    li a3, XIP_QMI_BASE

#ifdef PROGRAM_CR2_LATENCY
_program_cr2:
    li a1, INIT_DIRECT_CSR
    sw a1, QMI_DIRECT_CSR_OFFSET(a3)
    // Wait for cooldown on last XIP transfer to expire, by polling BUSY
1:
    lw a1, QMI_DIRECT_CSR_OFFSET(a3)
    andi a1, a1, QMI_DIRECT_CSR_BUSY_BITS
    bnez a1, 1b

    // Preserve WREN when this function restores XIP between storage commands.
    // Only write CR2 when its latency actually needs changing.
    li a0, CMD_READ_CR2
    sw a0, QMI_DIRECT_TX_OFFSET(a3)
    li a0, 0xff
    sw a0, QMI_DIRECT_TX_OFFSET(a3)
    jal wait_qmi_ready
    lw a0, QMI_DIRECT_RX_OFFSET(a3)
    lw a0, QMI_DIRECT_RX_OFFSET(a3)
    li a2, CR2_VALUE
    beq a0, a2, _cr2_ready

    // Discard write-command RX bytes so WRAR cannot stall on the RX FIFO.
    li a0, QMI_DIRECT_TX_NOPUSH_BITS | CMD_WRITE_ENABLE
    sw a0, QMI_DIRECT_TX_OFFSET(a3)
    jal wait_qmi_ready

    // Pack five bytes into three FIFO entries (FIFO depth is four).
    // 16-bit entries transmit their least-significant byte first.
    li a0, QMI_DIRECT_TX_NOPUSH_BITS | QMI_DIRECT_TX_DWIDTH_BITS | CMD_WRITE_ANY_REGISTER
    sw a0, QMI_DIRECT_TX_OFFSET(a3)
    li a0, QMI_DIRECT_TX_NOPUSH_BITS | QMI_DIRECT_TX_DWIDTH_BITS | (REG_ADDR_CR2 << 8)
    sw a0, QMI_DIRECT_TX_OFFSET(a3)
    li a0, QMI_DIRECT_TX_NOPUSH_BITS | CR2_VALUE
    sw a0, QMI_DIRECT_TX_OFFSET(a3)
    jal wait_qmi_ready

    // Register writes require at least 5us with CS high. This loop exceeds
    // that at supported RP2350 system clocks (up to 150MHz).
    li a0, 2048
1:
    addi a0, a0, -1
    bnez a0, 1b
_cr2_ready:
    // Recover the direct-mode configuration after the cooldown poll.
    li a1, INIT_DIRECT_CSR
    // Disable direct mode
    andi a1, a1, ~QMI_DIRECT_CSR_EN_BITS
    sw a1, QMI_DIRECT_CSR_OFFSET(a3)
#endif

_qmi_config:
    li a0, INIT_M0_TIMING
    sw a0, QMI_M0_TIMING_OFFSET(a3)
    li a0, INIT_M0_RCMD
    sw a0, QMI_M0_RCMD_OFFSET(a3)
    li a0, INIT_M0_RFMT
    sw a0, QMI_M0_RFMT_OFFSET(a3)

    // Dummy transfer enters device XIP mode (Axh)
    li a1, XIP_NOCACHE_NOALLOC_BASE
    lw a1, (a1)

    // Set prefix length to 0, as the device no longer expects commands
    bclri a0, a0, QMI_M0_RFMT_PREFIX_LEN_LSB
    sw a0, QMI_M0_RFMT_OFFSET(a3)

// ----------------------------------------------------------------------------
// Arm implementation

#else

// On RP2350 boot stage2 is always called as a regular function, and should return normally
regular_func _stage2_boot
    push {lr}
_pad_config:
    ldr r3, =PADS_QSPI_BASE
    movs r0, INIT_PAD_SCLK
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SCLK_OFFSET]
    // SDx: disable input Schmitt to reduce delay
    adds r3, #REG_ALIAS_CLR_BITS
    movs r0, #PADS_QSPI_GPIO_QSPI_SD0_SCHMITT_BITS
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD0_OFFSET]
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD1_OFFSET]
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD2_OFFSET]
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD3_OFFSET]

    adds r3, (XIP_QMI_BASE - (PADS_QSPI_BASE + REG_ALIAS_CLR_BITS))

#ifdef PROGRAM_CR2_LATENCY
_program_cr2:
    ldr r1, =INIT_DIRECT_CSR
    str r1, [r3, #QMI_DIRECT_CSR_OFFSET]
    // Wait for cooldown on last XIP transfer to expire, by polling BUSY
1:
    ldr r0, [r3, #QMI_DIRECT_CSR_OFFSET]
    tst r0, #QMI_DIRECT_CSR_BUSY_BITS
    bne 1b

    // Preserve WREN when this function restores XIP between storage commands.
    // Only write CR2 when its latency actually needs changing.
    movs r0, #CMD_READ_CR2
    str r0, [r3, #QMI_DIRECT_TX_OFFSET]
    movs r0, #0xff
    str r0, [r3, #QMI_DIRECT_TX_OFFSET]
    bl wait_qmi_ready
    ldr r0, [r3, #QMI_DIRECT_RX_OFFSET]
    ldr r0, [r3, #QMI_DIRECT_RX_OFFSET]
    cmp r0, #CR2_VALUE
    beq _cr2_ready

    // Discard write-command RX bytes so WRAR cannot stall on the RX FIFO.
    ldr r0, =(QMI_DIRECT_TX_NOPUSH_BITS | CMD_WRITE_ENABLE)
    str r0, [r3, #QMI_DIRECT_TX_OFFSET]
    bl wait_qmi_ready

    // Pack five bytes into three FIFO entries (FIFO depth is four).
    // 16-bit entries transmit their least-significant byte first.
    ldr r0, =(QMI_DIRECT_TX_NOPUSH_BITS | QMI_DIRECT_TX_DWIDTH_BITS | CMD_WRITE_ANY_REGISTER)
    str r0, [r3, #QMI_DIRECT_TX_OFFSET]
    ldr r0, =(QMI_DIRECT_TX_NOPUSH_BITS | QMI_DIRECT_TX_DWIDTH_BITS | (REG_ADDR_CR2 << 8))
    str r0, [r3, #QMI_DIRECT_TX_OFFSET]
    ldr r0, =(QMI_DIRECT_TX_NOPUSH_BITS | CR2_VALUE)
    str r0, [r3, #QMI_DIRECT_TX_OFFSET]
    bl wait_qmi_ready

    // Register writes require at least 5us with CS high. This loop exceeds
    // that at supported RP2350 system clocks (up to 150MHz).
    movw r0, #2048
1:
    subs r0, #1
    bne 1b
_cr2_ready:
    // Disable direct mode
    bics r1, #QMI_DIRECT_CSR_EN_BITS
    str r1, [r3, #QMI_DIRECT_CSR_OFFSET]
#endif

_qmi_config:
    ldr r0, =INIT_M0_TIMING
    str r0, [r3, #QMI_M0_TIMING_OFFSET]
    ldr r0, =INIT_M0_RCMD
    str r0, [r3, #QMI_M0_RCMD_OFFSET]
    ldr r0, =INIT_M0_RFMT
    str r0, [r3, #QMI_M0_RFMT_OFFSET]

    // Dummy transfer enters device XIP mode (Axh)
    mov r1, #XIP_NOCACHE_NOALLOC_BASE
    ldrb r1, [r1]

    // Set prefix length to 0, as the device no longer expects commands
    bic r0, #QMI_M0_RFMT_PREFIX_LEN_BITS
    str r0, [r3, #QMI_M0_RFMT_OFFSET]

#endif

// ----------------------------------------------------------------------------

// Pull in standard exit routine
#include "boot2_helpers/exit_from_boot2.S"

// Common functions
#ifdef PROGRAM_CR2_LATENCY
#include "boot2_helpers/wait_qmi_ready.S"
#endif

#ifndef __riscv
.global literals
literals:
.ltorg
#endif
