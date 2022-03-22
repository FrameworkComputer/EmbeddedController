# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F412
CHIP:=stm32
CHIP_FAMILY:=stm32f4
CHIP_VARIANT:=stm32f412

# Don't forget that the board build.mk is included more than once to allow
# conditional variables to be realized. This means that we need to redefine all
# variable or the "+=" lines will compound.
board-rw=board_rw.o
board-y=board.o

# If we're mocking the sensor detection for testing (so we can test
# sensor/transport permutations in the unit tests), don't build the real sensor
# detection.
ifeq ($(HAS_MOCK_FPSENSOR_DETECT),)
	board-y+=fpsensor_detect.o
	board-rw+=fpsensor_detect_rw.o
endif

# Do not build rsa test because this board uses RSA exponent 3 and the rsa test
# will fail on device.
test-list-y=\
       aes \
       cec \
       compile_time_macros \
       cortexm_fpu \
       crc \
       flash_physical \
       flash_write_protect \
       fpsensor \
       fpsensor_hw \
       mpu \
       mutex \
       panic_data \
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
       stm32f_rtc \
       system_is_locked \
       timer_dos \
       utils \
       utils_str \
