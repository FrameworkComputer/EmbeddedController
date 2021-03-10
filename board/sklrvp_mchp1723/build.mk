# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Intel SKL-RVP fly wired to MEC152x EVB board-specific configuration
#

# the IC is Microchip MEC172x 416 KB total SRAM
# MEC1723SZ variant is 144 pin, loads from external SPI flash
# MEC1727SZ variant is 144 pin, loads from 512KB internal SPI flash
# external SPI is 512KB
# external clock is crystal
CHIP:=mchp
CHIP_FAMILY:=mec172x
CHIP_VARIANT:=mec1723sz
CHIP_SPI_SIZE_KB:=512
BASEBOARD:=intelrvp

board-y=board.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
