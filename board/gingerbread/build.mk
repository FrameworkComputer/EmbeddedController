# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=stm32
# TODO(b/148493929): The chip family for honeybuns is STM32G4. The chip
# variant is STM32G431x. Support for this chip is not yet in the Cros EC
# codebase. Currently, using a variant of the F family so the project will
# build properly.
CHIP_FAMILY:=stm32g4
CHIP_VARIANT:=stm32g431xb
BASEBOARD:=honeybuns

board-y=board.o
