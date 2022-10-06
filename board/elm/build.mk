#-*- makefile -*-
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# STmicro STM32F091VC
CHIP := stm32
CHIP_FAMILY := stm32f0
CHIP_VARIANT:= stm32f09x

board-y = board.o battery.o led.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
