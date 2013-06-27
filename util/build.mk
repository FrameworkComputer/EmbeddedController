# -*- makefile -*-
# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Host tools build
#

host-util-bin=ectool lbplay burn_my_ec
host-util-common=ectool_keyscan comm-host comm-dev misc_util ec_flash
ifeq ($(CONFIG_LPC),y)
host-util-common+=comm-lpc
else
host-util-common+=comm-i2c
endif
build-util-bin=ec_uartd stm32mon
