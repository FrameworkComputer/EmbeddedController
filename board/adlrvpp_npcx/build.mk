# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Intel ADL-P-RVP-NPCX board-specific configuration
#

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m3f
BASEBOARD:=intelrvp

board-y=board.o
