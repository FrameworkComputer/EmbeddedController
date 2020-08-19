# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
# A limited Voxe boards are reworked with NPCX797FC variant. Set the
# modify the variant type to match.
ifeq ($(BOARD),voxel_npcx797fc)
CHIP_VARIANT:=npcx7m7fc
else
CHIP_VARIANT:=npcx7m6fc
endif
BASEBOARD:=volteer

board-y=board.o
board-y+=battery.o
board-y+=led.o
board-y+=sensors.o
