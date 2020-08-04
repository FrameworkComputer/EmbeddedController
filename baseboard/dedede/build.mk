# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

baseboard-y=baseboard.o cbi_fw_config.o
baseboard-$(VARIANT_DEDEDE_EC_NPCX796FC)+=variant_ec_npcx796fc.o
baseboard-$(VARIANT_DEDEDE_EC_IT8320)+=variant_ec_it8320.o
