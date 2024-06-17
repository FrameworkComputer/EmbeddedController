# Fingerprint MCU RAM and Flash

[TOC]

## Overview

The fingerprint firmware divides flash into three sections: RO, Rollback, and
RW. The RO (Read-only) section contains a minimal set of code to check the
signature of the code in RW; it’s essentially a bootloader and once a device
enters production it never changes. The RW section is the portion of flash that
runs the matching algorithm and is the portion that we update over time. The
Rollback section consists of two separate flash sectors that store the minimum
RW version that is allowed to run; this prevents attacks where an older version
of RW firmware has a security vulnerability, and we want to prevent it from ever
being loaded again (even though it’s valid and will pass the signature check).
The fingerprint firmware also uses the rollback section to store the entropy
used for the encryption key that protects the fingerprint templates.

<!-- mdformat off(b/139308852) -->
*** note
The amount of RAM and flash used is specific to the sensor and matching
algorithm used. After building the FPMCU firmware (e.g.,
[`make BOARD=bloonchipper`]), the amount of flash and RAM *remaining* will be
printed out.
***
<!-- mdformat on -->

## STM32H743 (Dartmonkey)

Total Flash: [2 MB]

Total SRAM: [1 MB], but [864 KB is "user SRAM"]. See [Section 2.4 Embedded SRAM]
for more details.

### Flash

Flash section   | Size                                        | Amount Used
--------------- | ------------------------------------------- | -----------
Read-only (RO)  | [768 KB]                                    | See [`make BOARD=dartmonkey`] output.
Rollback        | [256 KB total][256 KB] (two 128 KB sectors) | [44 bytes in each of the two sections]
Read-write (RW) | [1024 KB]                                   | See [`make BOARD=dartmonkey`] output.

#### Layout

Flash sector | Flash sector size | Offset             | Firmware Region
------------ | ----------------- | ------------------ | -------------------
1            | 128 KB            | 0                  | RO ([768 KB])
2            | 128 KB            | 0x20000 (131072)   | ...
3            | 128 KB            | 0x40000 (262144)   | ...
4            | 128 KB            | 0x60000 (393216)   | ...
5            | 128 KB            | 0x80000 (524288)   | ...
6            | 128 KB            | 0xA0000 (655360)   | ...
7            | 128 KB            | 0xC0000 (786432)   | Rollback ([256 KB])
8            | 128 KB            | 0xE0000 (917504)   | ...
9            | 128 KB            | 0x100000 (1048576) | RW ([1024 KB])
10           | 128 KB            | 0x120000 (1179648) | ...
11           | 128 KB            | 0x140000 (1310720) | ...
12           | 128 KB            | 0x160000 (1441792) | ...
13           | 128 KB            | 0x180000 (1572864) | ...
14           | 128 KB            | 0x1A0000 (1703936) | ...
15           | 128 KB            | 0x1C0000 (1835008) | ...
16           | 128 KB            | 0x1E0000 (1966080) | ...

[2 MB]: https://www.st.com/resource/en/datasheet/stm32h743vi.pdf
[1 MB]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/chip/stm32/memory_regions.inc;l=10-16;drc=eee7778fcfc5e555d119cff65caa7c045343e356
[864 KB is "user SRAM"]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/chip/stm32/memory_regions.inc;l=13;drc=eee7778fcfc5e555d119cff65caa7c045343e356
[Section 2.4 Embedded SRAM]: https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf#page=135
[`make BOARD=dartmonkey`]: ./fingerprint.md
[`make BOARD=bloonchipper`]: ./fingerprint.md
[`make BOARD=helipilot`]: ./fingerprint.md
[256 KB]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/board/nocturne_fp/board.h;l=62;drc=ed42e86fd3c444982aa7c313c4530ad89c310191
[768 KB]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/board/nocturne_fp/board.h;l=58;drc=ed42e86fd3c444982aa7c313c4530ad89c310191
[1024 KB]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/board/nocturne_fp/board.h;l=66-67;drc=ed42e86fd3c444982aa7c313c4530ad89c310191
[44 bytes in each of the two sections]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/common/rollback_private.h;l=21-29;drc=675f3a1a481aae3247e2d2af3e603b149cdaf4aa

## STM32F412CG (Bloonchipper)

Total Flash: [1 MB][1 MB STM32F412]

Total SRAM: [256 KB]

### Flash

Flash section   | Size                                              | Amount Used
--------------- | ------------------------------------------------- | -----------
Read-only (RO)  | [128 KB]                                          | See [`make BOARD=bloonchipper`] output.
Rollback        | [256 KB (two 128 KB sectors)][STM32F412 Rollback] | [44 bytes in each of the two sections]
Read-write (RW) | 640 KB                                            | See [`make BOARD=bloonchipper`] output.

#### Layout

Flash sector | Flash sector size | Offset           | Firmware Region
------------ | ----------------- | ---------------- | ---------------
1            | 16 KB             | 0                | RO ([128 KB])
2            | 16 KB             | 0x4000 (16384)   | ...
3            | 16 KB             | 0x8000 (32768)   | ...
4            | 16 KB             | 0xC000 (49152)   | ...
5            | 64 KB             | 0x10000 (65536)  | ...
6            | 128 KB            | 0x20000 (131072) | Rollback ([256 KB][STM32F412 Rollback])
7            | 128 KB            | 0x40000 (262144) | ...
8            | 128 KB            | 0x60000 (393216) | RW (640KB)
9            | 128 KB            | 0x80000 (524288) | ...
10           | 128 KB            | 0xA0000 (655360) | ...
11           | 128 KB            | 0xC0000 (786432) | ...
12           | 128 KB            | 0xE0000 (917504) | ...

[1 MB STM32F412]: https://www.st.com/resource/en/datasheet/stm32f412cg.pdf
[256 KB]: https://www.st.com/resource/en/datasheet/stm32f412cg.pdf
[128 KB]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/board/hatch_fp/board.h;l=66;drc=643635b91ee40def104188cf9623a9a28f25bd4f
[STM32F412 Rollback]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/board/hatch_fp/board.h;l=70;drc=643635b91ee40def104188cf9623a9a28f25bd4f

## NPCX99FP (Helipilot)

Total Flash: [1 MB][NPCX99FP Reference Manual]

Total SRAM: [512 KB][NPCX99FP Reference Manual]

`helipilot` is different from the other FPMCU boards in that it cannot execute
code quickly from Flash. Instead, all code is loaded in RAM and execute from
RAM. This means that RAM must be partitioned into Code RAM and Data RAM.

### RAM Partition

Description                | Size
-------------------------- | ------------------------------
Code RAM                   | [352 KB][helipilot Code RAM]
Data RAM                   | [156 KB][helipilot Data RAM]
Data RAM for ROM functions | [4 KB][helipilot ROM Data RAM]

[helipilot Code RAM]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard/helipilot/base_board.h;l=65-71;drc=c0f65dc488d48c4c9634276520337dcde4f57107
[helipilot Data RAM]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard/helipilot/base_board.h;l=73-79;drc=c0f65dc488d48c4c9634276520337dcde4f57107
[helipilot ROM Data RAM]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard/helipilot/base_board.h;l=81-86;drc=c0f65dc488d48c4c9634276520337dcde4f57107

### Flash

Flash section               | Size                                             | Amount Used
--------------------------- | ------------------------------------------------ | -----------
[Nuvoton bootloader header] | [4 KB][helipilot RO]                             | [64 bytes][NPCX firmware header]
Read-only (RO)              | [124 KB][helipilot RO]                           | See [`make BOARD=helipilot`] output.
Rollback                    | [128 KB (two 64 KB sectors)][helipilot Rollback] | [44 bytes in each of the two sections]
Read-write (RW)             | 768 KB ([320 KB usable][helipilot RW])           | See [`make BOARD=helipilot`] output. Note that the usable RW size is [limited by the amount of SRAM][helipilot RW].

[helipilot RO]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard/helipilot/base_board.h;l=122;drc=c0f65dc488d48c4c9634276520337dcde4f57107
[helipilot Rollback]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard/helipilot/base_board.h;l=297;drc=c0f65dc488d48c4c9634276520337dcde4f57107
[helipilot RW]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/baseboard/helipilot/base_board.h;l=141-148;drc=c0f65dc488d48c4c9634276520337dcde4f57107
[Nuvoton bootloader header]: http://go/cros-fp-npcx99fp-bootloader
[NPCX firmware header]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/chip/npcx/header.c;l=43-58;drc=88684b2a25d0349bcd57cdba2a5aadb60d7bef07

### Flash Layout

NPCX99FP supports erase size of 64 KB, 32 KB, and 4 KB. Our code configures it
with a [uniform flash sector size of 64 KB][NPCX99FP sector size].

[NPCX99FP sector size]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/chip/npcx/config_flash_layout.h;l=103-110;drc=29c7712541e8f85c53bac4d23147604585a764d8
[NPCX99FP Reference Manual]: http://go/cros-fp-npcx99fp-ref-manual
