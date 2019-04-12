# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Hatch baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-$(CONFIG_BATTERY_SMART)+=battery.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
