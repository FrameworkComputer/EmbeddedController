# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
# A limited Magolor_legacy boards are reworked with NPCX796FC variant.
# Set the modify the variant type to match.
ifeq ($(BOARD),magolor_legacy)
CHIP_VARIANT:=npcx7m6fc
else
CHIP_VARIANT:=npcx7m7fc
endif
BASEBOARD:=dedede

board-y=board.o battery.o cbi_ssfc.o led.o usb_pd_policy.o
