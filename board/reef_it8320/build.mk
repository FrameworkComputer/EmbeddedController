# -*- makefile -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#
# the IC is ITE IT8320
CHIP:=it83xx
CHIP_FAMILY:=it8320
CHIP_VARIANT:=it8320dx

board-y=board.o led.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
