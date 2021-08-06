# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32H743
CHIP:=stm32
CHIP_FAMILY:=stm32h7
CHIP_VARIANT:=stm32h7x3

board-rw=ro_workarounds.o board_rw.o
board-y=board.o
# If we're mocking the sensor detection for testing (so we can test
# sensor/transport permutations in the unit tests), don't build the real sensor
# detection.
ifeq ($(HAS_MOCK_FPSENSOR_DETECT),)
	board-rw+=fpsensor_detect_rw.o
	board-y+=fpsensor_detect.o
endif

# Do not build rsa test because this board uses RSA exponent 3 and the rsa test
# will fail on device.
test-list-y=\
       aes \
       cec \
       compile_time_macros \
       crc \
       flash_physical \
       flash_write_protect \
       fpsensor \
       fpsensor_hw \
       mpu \
       mutex \
       pingpong \
       printf \
       queue \
       rollback \
       rollback_entropy \
       rsa3 \
       rtc \
       scratchpad \
       sha256 \
       sha256_unrolled \
       static_if \
       timer_dos \
       utils \
       utils_str \
