# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build


# TODO(b/132204142): Sweetberry's USB connection fails to initialize properly
# when built using the 8.2.0 tool chain leave it with the 4.9.x.
CROSS_COMPILE_arm:=arm-none-eabi-

CHIP:=stm32
CHIP_FAMILY:=stm32f4
CHIP_VARIANT:=stm32f446

board-y=board.o
