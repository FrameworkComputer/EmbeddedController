# -*- makefile -*-
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-y+=led.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o usb_pd_pdo.o
