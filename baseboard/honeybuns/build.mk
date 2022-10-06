# -*- makefile -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Honeybuns baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
baseboard-y+=usbc_support.o
