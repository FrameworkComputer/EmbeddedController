# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-y+=board_chipset.o
baseboard-y+=board_id.o
baseboard-y+=hibernate.o
baseboard-y+=regulator.o
baseboard-y+=usbc_config.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
