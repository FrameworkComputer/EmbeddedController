# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7

# Old Delbin boards are using with NPCX796FC variant.
ifeq ($(BOARD),delbin_npcx796fc)
CHIP_VARIANT:=npcx7m6fc
else
CHIP_VARIANT:=npcx7m7fc
endif
BASEBOARD:=volteer

board-y=board.o
board-y+=battery.o
board-y+=led.o
board-y+=sensors.o
