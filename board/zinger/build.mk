# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F031F6
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f03x

board-y=board.o hardware.o runtime.o usb_pd_policy.o
board-$(CONFIG_DEBUG_PRINTF)+=debug.o

# Add dependency to generate the public key coefficients header
$(out)/board/$(BOARD)/board.o: $(out)/gen_pub_key.h
