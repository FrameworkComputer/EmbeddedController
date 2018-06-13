# -*- makefile -*-
# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# Board is only valid for host tools
CHIP:=npcx
CHIP_VARIANT:=npcx5m5g

board-y=board.o
