# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32H743
CHIP:=stm32
CHIP_FAMILY:=stm32h7
CHIP_VARIANT:=stm32h7x3

# Don't forget that the board build.mk is included more than once to allow
# conditional variables to be realized. This means that we need to redefine all
# variable or the "+=" lines will compound.
board-y=
board-rw=ro_workarounds.o board_rw.o
board-ro=board_ro.o
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
       always_memset \
       cec \
       compile_time_macros \
       cortexm_fpu \
       crc \
       debug \
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
       stdlib \
       system_is_locked \
       timer_dos \
       utils \
       utils_str \

# Note that this variable includes the trailing "/"
_nocturne_fp_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))
-include $(_nocturne_fp_cur_dir)../../private/board/nocturne_fp/build.mk
