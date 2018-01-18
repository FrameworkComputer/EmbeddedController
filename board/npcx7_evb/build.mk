# -*- makefile -*-
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Nuvoton NPCX7 M-Series EC (npcx7m6g, npcx7m6f, npcx7m6xb, npcx7m7w)
# CHIP_VARIANT:
#    npcx7m6f  - for npcx7 ec with 144 pins package
#    npcx7m6g  - for npcx7 ec with 128 pins package
#    npcx7m6xb - for npcx7 ec with 144 pins package, enhanced features.
#    npcx7m7w  - for npcx7 ec with 144 pins package, enhanced features + WOV.

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m7w

board-y=board.o
