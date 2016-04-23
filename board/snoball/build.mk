# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F030C8
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f03x8

board-y=board.o

# This target builds RW only.  Therefore, remove RO from dependencies.
all_deps=$(patsubst ro,,$(def_all_deps))
