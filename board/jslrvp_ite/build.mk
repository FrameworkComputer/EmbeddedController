# -*- makefile -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Intel Jasperlake RVP with ITE EC board specific configuration
#

#it8320
CHIP:=it83xx
CHIP_FAMILY:=it8320
CHIP_VARIANT:=it8320dx
BASEBOARD:=intelrvp

board-y=board.o
