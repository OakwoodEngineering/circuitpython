USB_VID = 0x2E8A
USB_PID = 0x000C	# XXX
USB_PRODUCT = "Obi Wan Komputer (MRAM)"
USB_MANUFACTURER = "NyanSat"

CHIP_VARIANT = RP2350
CHIP_PACKAGE = B
CHIP_FAMILY = rp2

EXTERNAL_FLASH_DEVICES = "W25Q128JVxQ"

# AS3016204-0108X0IWAY: EBh + F0h, 1-4-4 SDR, 12 latency clocks.
# Divider 4: 37.5 MHz at CPU 150 MHz; autonomous OTP boot qualified 2026-09-12.
# CR2 persists: subsequent reset/power-on needs the SRAM/OTP repair loader.
# Datasheet Table 22: 12..15 cycles, up to the part limit of 108 MHz.
BOOT2_SOURCE = boot_stage2/boot2_asxxxx204.S
BOOT2_S_CFLAGS = -DPICO_FLASH_SPI_CLKDIV=4 -DPICO_FLASH_SPI_RXDELAY=2 -DPICO_MRAM_XIP_READ_LATENCY=12

# ASxxxx204 16Mbit MRAM (2MiB). Software bounds exclude the first 1MiB from FAT writes.
USE_MRAM_STORAGE = 1
CFLAGS += -DCIRCUITPY_MRAM_TOTAL_BYTES='(2 * 1024 * 1024)'
# Keep a small 4KiB NVM region between firmware and filesystem.
CFLAGS += -DCIRCUITPY_INTERNAL_NVM_SIZE='(4 * 1024)'

CIRCUITPY__EVE = 1
