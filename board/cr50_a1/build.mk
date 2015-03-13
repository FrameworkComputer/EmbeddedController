# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

CHIP:=g
CHIP_FAMILY:=cr50
CHIP_VARIANT:=cr50_a1

board-y=board.o

# Need to generate a .hex file
all: hex
