# NyanSat OBK MRAM

This target is the RP2350B prototype with Avalanche **AS3016204-0108X0IWAY**
(2 MiB MRAM on QMI CS0) and 8 MiB PSRAM on CS1 / GPIO0. PSRAM is believed to be
VTI7064MSME; its physical marking was not independently confirmed.

## Boot prerequisite and operating format

The prototype has a programmed 1384-byte OTP repair image. On reset, RP2350 ROM
copies it into SRAM. It resets the MRAM interface, checks device ID `e6010401`,
normalizes SR/CR1/CR2/CR3/CR4 to `00/01/00/20/05`, and calls ROM `chain_image`
to enter CircuitPython from MRAM. It does not write program or filesystem bytes.

CircuitPython's startup executes the selected boot2 from SRAM. It switches MRAM
to EB/F0 **1-4-4 SDR**, 12 latency clocks, divider 4, RX delay 2: 37.5 MHz with
CPU 150 MHz. Every read has a command prefix. MRAM is not in 4-4-4 QPI or
commandless continuous-read mode. CR2=`0C` persists through power removal;
the independent early repair is needed before ROM can reach this startup code.
Flashing this quad firmware alone onto a virgin-OTP board does not establish
autonomous reboot. Ordinary firmware updates on this provisioned board need no
additional OTP writes.

PSRAM independently uses 4-4-4 QPI at 75 MHz, RX delay 1, six read dummy clocks.
The board sets MAX_SELECT=6 to accommodate the believed part's 4 us CS-active
limit; other boards retain the port default of 16. CPU speed remains 150 MHz.

## Memory and storage

Ranges have exclusive ends; offsets are relative to MRAM, not CPU addresses.

| MRAM offset | Size | Use |
|---|---|---|
| `0x000000..0x0ff000` | 1020 KiB | Firmware reservation, mapped at `0x10000000`. |
| `0x0ff000..0x100000` | 4 KiB | `microcontroller.nvm`. |
| `0x100000..0x200000` | 1 MiB | CIRCUITPY FAT filesystem, including `code.py`. |

Firmware mostly executes through MRAM XIP; selected code/data and the storage
transaction routines live in RP2350 SRAM. The internal TLSF heap is extended by
8 MiB volatile PSRAM at `0x11000000`. The saved Python application is in MRAM
FAT, not PSRAM. ROM reuses the early patch SRAM for normal application startup.

`USE_MRAM_STORAGE=1` selects the MRAM backend. FAT has 512-byte blocks and a
4 KiB SRAM sector cache. Flushes use WREN/WRITE transactions from SRAM, with
no NOR erase. Filesystem writes are bounded to the upper half; the separate
NVM helper is bounded to its 4 KiB region. These are software checks; hardware
MRAM block protection is not enabled. OTP clears protection selections at boot.

## Build and maintenance

From this repository root, after installing the normal CircuitPython build
prerequisites and submodules:

```sh
make -C ports/raspberrypi BOARD=nyansat_obk_mram \
  BUILD=build-nyansat_obk_mram_ready -j8
```

Use a fresh build directory when changing board/boot2 headers. This port uses
its own `ports/raspberrypi/sdk` submodule, not the sibling `../pico-sdk` checkout.
The qualified build used SDK commit `bddd20f928ce76142793bef434d4f75f4af6e433`.

The sibling pico-sdk repository contains the SRAM SWD writer and canonical
programming command in `tools/mram_swd/docs/nyansat_obk_bringup.md`: writer
divider 4/RX2, SWD 2 MHz, mandatory target CRC64 plus byte readback. Program the
firmware UF2 only to preserve NVM/FAT. Do not replay OTP provisioning on this board.

The exact qualified firmware is archived there as
`tools/mram_swd/review/2026-09-11-div4/circuitpython-37_5mhz.bin` (836720 bytes),
SHA256 `15cea20f72802874bf69c5b498658a560e68cd0af0edeb7251861d15dcd08391`.
Build/version metadata may change a rebuild's hash. The final source's board
Makefile comments were updated after qualification; executable settings are
unchanged from the frozen build.

The full-brightness GPIO15 rainbow uses built-in `neopixel_write`; its source
is in the sibling repo at `tools/mram_swd/examples/rainbow.py`, installed as
`CIRCUITPY/code.py`. No external Python libraries are required.

## Qualification record

On 2026-09-12 the selected firmware passed repeated boots, retained-register
repair, interrupted-repair recovery, a 7.5 MiB PSRAM sweep, physical OTP copying,
enabled ROM boot, and a final autonomous power-removal boot with probe USB
unplugged. USB-only follow-up verified memory/timings and rainbow restart.
75 MHz MRAM had an intermittent startup-fetch error and is not the selected mode.
Physical BOOTSEL recovery and continuous-read/sleep recovery remain unqualified;
watchdog testing was excluded from the final plan.

Detailed documents in sibling `../pico-sdk/tools/mram_swd/docs/`:

- `system_architecture.md`: boot stages, device states and update/recovery paths.
- `memory_architecture.md`: address maps, OTP rows, SRAM lifetimes and caches.
- `otp_programming_results_2026-09-12.md`: exact hashes and test outcomes.

Frozen review packages and raw per-command evidence are preserved under that
repository's `tools/mram_swd/review/` and `tools/mram_swd/captures/`.
