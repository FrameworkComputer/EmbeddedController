# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is SMSC MEC1322 / external SPI is 4MB / external clock is crystal
CHIP:=mec1322
CHIP_SPI_SIZE_KB:=4096

board-y=board.o
board-$(CONFIG_BATTERY_SMART)+=battery.o

# As this file is read more than once, must put the rules
# elsewhere (Makefile.rules) and just use variable to trigger them
PROJECT_EXTRA+=${out}/ec.spi.bin
