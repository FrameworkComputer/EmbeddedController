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

# Use coreboot-sdk
$(call set-option,CROSS_COMPILE_arm,\
	$(CROSS_COMPILE_coreboot_sdk_arm),\
	/opt/coreboot-sdk/bin/arm-eabi-)

board-y=battery.o board.o usb_pd_policy.o led.o
board-$(CONFIG_BOOTBLOCK)+=emmc.o

$(out)/RO/board/$(BOARD)/emmc.o: $(out)/bootblock_data.h
