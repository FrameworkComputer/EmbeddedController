# -*- makefile -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#
#
# STmicro STM32L431RCI
CHIP:=stm32
CHIP_FAMILY:=stm32l4
CHIP_VARIANT:=stm32l431x
BASEBOARD:=kukui

board-y=battery.o board.o led.o
