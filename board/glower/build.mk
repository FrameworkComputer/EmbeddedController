# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Microchip MEC1322
CHIP:=mec1322
CHIP_SPI_SIZE_KB:=512

board-y=board.o
# As this file is read more than once, must put the rules
# elsewhere (Makefile.rules) and just use variable to trigger them
PROJECT_EXTRA+=${out}/ec.spi.bin
