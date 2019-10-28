# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m6fc

board-y=board.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
board-y+=led.o
