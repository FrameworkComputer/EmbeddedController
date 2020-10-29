# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F412
CHIP:=stm32
CHIP_FAMILY:=stm32f4
CHIP_VARIANT:=stm32f412

board-y=board.o

# If we're not building a test build, use the real sensor detection. Otherwise,
# for test builds, allow the mock version so we can test sensor/transport
# permutations in the unit tests.
ifeq ($(TEST_BUILD),)
board-y+=fpsensor_detect.o
endif

test-list-y=\
       aes \
       compile_time_macros \
       crc32 \
       flash_physical \
       flash_write_protect \
       fpsensor \
       mpu \
       mutex \
       pingpong \
       rollback \
       rollback_entropy \
       rsa \
       rsa3 \
       rtc \
       scratchpad \
       sha256 \
       sha256_unrolled \
       stm32f_rtc \
       utils \
