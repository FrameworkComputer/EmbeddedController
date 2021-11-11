# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#
# Reworked drawcia with it8320 replaced with RISC-V it82302
#

CHIP:=it83xx
CHIP_FAMILY:=it8xxx2
CHIP_VARIANT:=it81302bx_512
BASEBOARD:=dedede

board-y=board.o cbi_ssfc.o led.o usb_pd_policy.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
