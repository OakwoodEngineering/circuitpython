// ----------------------------------------------------------------------------
// Second stage boot code
// Copyright (c) 2019-2021 Raspberry Pi (Trading) Ltd.
// SPDX-License-Identifier: BSD-3-Clause
//
// Device:      Avalanche ASxxxx204 MRAM, ROM-compatible serial reads
//
// Details:     * Keep CR2 latency at zero, as required by ROM READ 03h.
//              * MRAM configuration survives power loss. Continuous quad XIP
//                requires latency >=8, which prevents subsequent ROM boot.
//              * This mode retains normal memory-mapped execution without
//                changing device registers. Provision CR2=0 with the SWD writer.
//              * At 150MHz, divider 4 gives 37.5MHz (below READ's 50MHz limit).
//
// Building:    * This code must be position-independent, and use stack only
//              * The code will be padded to a size of 256 bytes, including a
//                4-byte checksum. Therefore code size cannot exceed 252 bytes.
// ----------------------------------------------------------------------------

#include "pico/asm_helper.S"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/pads_qspi.h"

// ----------------------------------------------------------------------------
// Config section
// ----------------------------------------------------------------------------
// It should be possible to support most flash devices by modifying this section

// The serial flash interface will run at clk_sys/PICO_FLASH_SPI_CLKDIV.
// This must be a positive integer.
// The bootrom is very conservative with SPI frequency, but here we should be
// as aggressive as possible.

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 4
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

#define CMD_READ 0x03

// ----------------------------------------------------------------------------
// Register initialisation values -- same in Arm/RISC-V code.
// ----------------------------------------------------------------------------

// The QMI is automatically configured for 03h XIP straight out of reset,
// but this code can't assume it's still in that state. Set up memory
// window 0 for 03h serial reads.

// Setup timing parameters: short sequential-access cooldown, configured
// CLKDIV and RXDELAY, and at least 20ns CS-high recovery at 150MHz.

#define INIT_M0_TIMING (\
    1                      << QMI_M0_TIMING_COOLDOWN_LSB |\
    3                      << QMI_M0_TIMING_MIN_DESELECT_LSB |\
    PICO_FLASH_SPI_RXDELAY << QMI_M0_TIMING_RXDELAY_LSB |\
    PICO_FLASH_SPI_CLKDIV  << QMI_M0_TIMING_CLKDIV_LSB |\
0)

// Set command constants
#define INIT_M0_RCMD (\
    CMD_READ             << QMI_M0_RCMD_PREFIX_LSB |\
0)

// Set read format to all-serial with a command prefix
#define INIT_M0_RFMT (\
    QMI_M0_RFMT_PREFIX_WIDTH_VALUE_S << QMI_M0_RFMT_PREFIX_WIDTH_LSB |\
    QMI_M0_RFMT_ADDR_WIDTH_VALUE_S   << QMI_M0_RFMT_ADDR_WIDTH_LSB |\
    QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_S << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |\
    QMI_M0_RFMT_DUMMY_WIDTH_VALUE_S  << QMI_M0_RFMT_DUMMY_WIDTH_LSB |\
    QMI_M0_RFMT_DATA_WIDTH_VALUE_S   << QMI_M0_RFMT_DATA_WIDTH_LSB |\
    QMI_M0_RFMT_PREFIX_LEN_VALUE_8   << QMI_M0_RFMT_PREFIX_LEN_LSB |\
0)

// ----------------------------------------------------------------------------
// Start of 2nd Stage Boot Code
// ----------------------------------------------------------------------------

pico_default_asm_setup

.section .text

// On RP2350 boot stage2 is always called as a regular function, and should return normally
regular_func _stage2_boot
#ifdef __riscv
    mv t0, ra
    li a3, PADS_QSPI_BASE
    li a0, (2 << PADS_QSPI_GPIO_QSPI_SCLK_DRIVE_LSB) | PADS_QSPI_GPIO_QSPI_SCLK_SLEWFAST_BITS
    sw a0, PADS_QSPI_GPIO_QSPI_SCLK_OFFSET(a3)
    li a3, PADS_QSPI_BASE + REG_ALIAS_CLR_BITS
    li a0, PADS_QSPI_GPIO_QSPI_SD0_SCHMITT_BITS
    sw a0, PADS_QSPI_GPIO_QSPI_SD0_OFFSET(a3)
    sw a0, PADS_QSPI_GPIO_QSPI_SD1_OFFSET(a3)
    sw a0, PADS_QSPI_GPIO_QSPI_SD2_OFFSET(a3)
    sw a0, PADS_QSPI_GPIO_QSPI_SD3_OFFSET(a3)
    li a3, XIP_QMI_BASE
    li a0, INIT_M0_TIMING
    sw a0, QMI_M0_TIMING_OFFSET(a3)
    li a0, INIT_M0_RCMD
    sw a0, QMI_M0_RCMD_OFFSET(a3)
    li a0, INIT_M0_RFMT
    sw a0, QMI_M0_RFMT_OFFSET(a3)
#else
    push {lr}
    ldr r3, =PADS_QSPI_BASE
    movs r0, (2 << PADS_QSPI_GPIO_QSPI_SCLK_DRIVE_LSB) | PADS_QSPI_GPIO_QSPI_SCLK_SLEWFAST_BITS
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SCLK_OFFSET]
    adds r3, #REG_ALIAS_CLR_BITS
    movs r0, #PADS_QSPI_GPIO_QSPI_SD0_SCHMITT_BITS
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD0_OFFSET]
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD1_OFFSET]
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD2_OFFSET]
    str r0, [r3, #PADS_QSPI_GPIO_QSPI_SD3_OFFSET]
    ldr r3, =XIP_QMI_BASE
    ldr r0, =INIT_M0_TIMING
    str r0, [r3, #QMI_M0_TIMING_OFFSET]
    ldr r0, =INIT_M0_RCMD
    str r0, [r3, #QMI_M0_RCMD_OFFSET]
    ldr r0, =INIT_M0_RFMT
    str r0, [r3, #QMI_M0_RFMT_OFFSET]
#endif

// Pull in standard exit routine
#include "boot2_helpers/exit_from_boot2.S"

#ifndef __riscv
.global literals
literals:
.ltorg
#endif
