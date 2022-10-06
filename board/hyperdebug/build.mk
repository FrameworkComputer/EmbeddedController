# -*- makefile -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F302R8
CHIP:=stm32
CHIP_FAMILY:=stm32l5
CHIP_VARIANT:=stm32l552xe

board-y=board.o
