# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#
#
# STmicro STM32F098VC
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f09x

board-y=battery.o board.o usb_pd_policy.o led.o
board-$(CONFIG_BOOTBLOCK)+=emmc.o
board-$(BOARD_KRANE)+=base_detect_krane.o

$(out)/RO/board/$(BOARD)/emmc.o: $(out)/bootblock_data.h

# 21504 bytes plus some extra buffer
DEFAULT_BOOTBLOCK_SIZE:=23000
