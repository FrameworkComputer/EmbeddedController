# -*- makefile -*-
# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Nuvoton NPCX5 M-Series EC (npcx5m5g, npcx5m6g)

CHIP:=npcx
CHIP_FAMILY:=npcx5
CHIP_VARIANT:=npcx5m5g

board-y=board.o
