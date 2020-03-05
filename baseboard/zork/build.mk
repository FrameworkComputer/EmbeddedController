# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-y+=cbi_ec_fw_config.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
baseboard-$(VARIANT_ZORK_TREMBYLE)+=variant_trembyle.o
baseboard-$(VARIANT_ZORK_DALBOZ)+=variant_dalboz.o
