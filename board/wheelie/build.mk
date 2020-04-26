# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=it83xx
CHIP_FAMILY:=it8320
CHIP_VARIANT:=it8320dx
BASEBOARD:=dedede

board-y=board.o led.o usb_pd_policy.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
