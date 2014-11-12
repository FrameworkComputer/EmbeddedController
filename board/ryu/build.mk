# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F373VB
CHIP:=stm32
CHIP_FAMILY:=stm32f3
CHIP_VARIANT:=stm32f373

board-y=board.o battery.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
