# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Intel SKL-RVP fly wired to MEC152x EVB board-specific configuration
#

# the IC is Microchip MEC1521
# external SPI is 512KB
# external clock is crystal
CHIP:=mchp
CHIP_FAMILY:=mec152x
CHIP_VARIANT:=mec1521
CHIP_SPI_SIZE_KB:=512
BASEBOARD:=intelrvp

board-y=board.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
