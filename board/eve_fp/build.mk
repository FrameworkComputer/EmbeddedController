# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicroelectronics STM32L442KC
CHIP:=stm32
CHIP_FAMILY:=stm32l4
CHIP_VARIANT:=stm32l442

board-y=board.o
