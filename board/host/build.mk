# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=host

board-y=board.o hyperdebug_util.o
board-$(HAS_TASK_CHIPSET)+=chipset.o
board-$(CONFIG_BATTERY_MOCK)+=battery.o charger.o
board-$(CONFIG_FANS)+=fan.o
board-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o usb_pd_config.o usb_pd_pdo.o
