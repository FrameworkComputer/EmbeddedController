# -*- makefile -*-
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=it83xx
CHIP_FAMILY:=it8320
CHIP_VARIANT:=it8320dx
BASEBOARD:=dedede

board-y=board.o cbi_ssfc.o keyboard_customization.o led.o usb_pd_policy.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
