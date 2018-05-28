# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F072RBT6
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f07x

# Build tests that we care about for hammer
test-list-y=entropy rsa3 sha256 sha256_unrolled x25519

board-y=board.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
