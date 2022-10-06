# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32L152RCT6
CHIP:=stm32
CHIP_FAMILY:=stm32l
CHIP_VARIANT:=stm32l15x

board-y=board.o
