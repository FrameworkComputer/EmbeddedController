# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o cbi_ssfc.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
baseboard-$(VARIANT_OCTOPUS_EC_NPCX796FB)+=variant_ec_npcx796fb.o
baseboard-$(VARIANT_OCTOPUS_EC_ITE8320)+=variant_ec_ite8320.o
baseboard-$(VARIANT_OCTOPUS_USBC_STANDALONE_TCPCS)+= \
	variant_usbc_standalone_tcpcs.o
baseboard-$(VARIANT_OCTOPUS_USBC_ITE_EC_TCPCS)+=variant_usbc_ec_tcpcs.o
