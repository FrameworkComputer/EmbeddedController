# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
baseboard-$(CONFIG_BOOTBLOCK)+=emmc.o

# TODO(b:137172860) split battery.c into variant_battery_xxx.c */
baseboard-y+=battery.o

$(out)/RO/baseboard/$(BASEBOARD)/emmc.o: $(out)/bootblock_data.h
