# -*- makefile -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=stm32
CHIP_FAMILY:=stm32g4
CHIP_VARIANT:=stm32g473xc
BASEBOARD:=honeybuns

board-y=board.o
