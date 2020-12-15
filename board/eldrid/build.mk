# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7

ifeq ($(BOARD),eldrid_npcx796)
CHIP_VARIANT:=npcx7m6fc
else
CHIP_VARIANT:=npcx7m7fc
endif
BASEBOARD:=volteer

board-y=board.o
board-y+=battery.o
board-y+=led.o
board-y+=sensors.o
board-y+=thermal.o
