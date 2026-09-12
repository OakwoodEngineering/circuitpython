// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2024 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#define MICROPY_HW_BOARD_NAME "NyanSat OBK MRAM"
#define MICROPY_HW_MCU_NAME "rp2350b"

#define MICROPY_HW_NEOPIXEL    (&pin_GPIO15)
#define MICROPY_HW_LED_STATUS  (&pin_GPIO24)

#define CIRCUITPY_BOARD_I2C         (2)
#define CIRCUITPY_BOARD_I2C_PIN     { \
	{.scl = &pin_GPIO35, .sda = &pin_GPIO30}, \
	{.scl = &pin_GPIO33, .sda = &pin_GPIO32}, \
}
#define CIRCUITPY_BOARD_I2C_BAUDRATE { \
	100000, \
	400000, \
}

#define CIRCUITPY_PSRAM_CHIP_SELECT (&pin_GPIO0)
// VTI7064MSME tCEM <= 4 us. At 150 MHz, 6 * 64 cycles = 2.56 us,
// leaving margin for a complete QPI command/address/dummy/8-byte cache line.
#define CIRCUITPY_PSRAM_MAX_SELECT (6)
