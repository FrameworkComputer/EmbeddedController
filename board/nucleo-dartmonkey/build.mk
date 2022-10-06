# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

BASEBOARD:=nucleo-h743zi

board-y=board.o
board-y+=fpsensor_detect.o

# Enable on device tests
test-list-y=\
       aes \
       cec \
       compile_time_macros \
       crc \
       debug \
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
       stdlib \
       timer_dos \
       utils \
       utils_str \
