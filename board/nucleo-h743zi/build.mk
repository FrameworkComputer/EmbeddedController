# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

BASEBOARD:=nucleo-h743zi

board-y=board.o

# Enable on device tests
test-list-y=\
       aes \
       cec \
       compile_time_macros \
       crc \
       debug \
       flash_physical \
       flash_write_protect \
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
