# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32TS60
CHIP:=stm32
CHIP_FAMILY:=stm32f
CHIP_VARIANT:=stm32ts60

board-y=board.o hardware.o runtime.o master_slave.o spi_comm.o touch_scan.o
board-y+=encode.o
board-$(CONFIG_DEBUG_PRINTF)+=debug.o
