# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Intel ADLRVP-P-DDR4-MEC1521 board-specific configuration
#

# the IC is Microchip MEC1521 with SRAM of 256KB
# external SPI is 32MB
# external clock is crystal
CHIP:=mchp
CHIP_FAMILY:=mec152x
CHIP_VARIANT:=mec1521
CHIP_SPI_SIZE_KB:=512
BASEBOARD:=intelrvp

board-y=board.o
