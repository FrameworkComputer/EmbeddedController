# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Nucleo-F412ZG baseboard specific files build
#

# the IC is STmicro STM32F412
CHIP:=stm32
CHIP_FAMILY:=stm32f4
CHIP_VARIANT:=stm32f412

baseboard-y=base-board.o
