# -*- makefile -*-
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Volteer baseboard specific files build
#

baseboard-y=baseboard.o
baseboard-y+=battery_presence.o
baseboard-y+=charger.o
baseboard-y+=usb_pd_policy.o
baseboard-y+=cbi.o
baseboard-y+=cbi_ec_fw_config.o
baseboard-y+=cbi_ssfc.o
baseboard-y+=power.o
baseboard-y+=usbc_config.o
