# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_VARIANT:=npcx5m6g

board-y=board.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
board-$(CONFIG_LED_COMMON)+=led.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
board-$(BOARD_LUX)+=base_detect_lux.o
board-$(BOARD_POPPY)+=base_detect_poppy.o
board-$(BOARD_SORAKA)+=base_detect_poppy.o
