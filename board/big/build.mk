# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32L100RBT6
CHIP:=stm32
CHIP_FAMILY:=stm32l
CHIP_VARIANT:=stm32l100

board-y=board.o battery.o led.o
