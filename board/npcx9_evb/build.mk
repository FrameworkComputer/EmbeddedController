# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Nuvoton NPCX9 M-Series EC (npcx9m3f, npcx9m6f)
# CHIP_VARIANT:
#    npcx9m6f  - for npcx9 ec with 512 KByte internal flash.
#    npcx9m3f  - for npcx9 ec with 512 KByte internal flash, more RAM.

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m3f

board-y=board.o
