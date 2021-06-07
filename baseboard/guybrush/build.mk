# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Guybrush baseboard specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m3f

baseboard-y=baseboard.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
baseboard-$(CONFIG_CBI_EEPROM)+=cbi.o