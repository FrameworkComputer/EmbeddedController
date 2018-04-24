# -*- makefile -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Nuvoton NPCX7 M-Series EC (npcx7m6g, npcx7m6f, npcx7m6fb, npcx7m7wb)
# CHIP_VARIANT:
#    npcx7m6g  - for npcx7 ec without internal flash
#    npcx7m6f  - for npcx7 ec with internal flash
#    npcx7m6fb - for npcx7 ec with internal flash, enhanced features.
#    npcx7m7wb - for npcx7 ec with internal flash, enhanced features + WOV.

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m7wb

board-y=board.o
