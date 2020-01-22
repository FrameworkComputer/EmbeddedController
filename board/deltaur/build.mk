# -*- makefile -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# Microchip MEC5106 which is similar to MEC1702
CHIP:=mchp
CHIP_FAMILY:=mec17xx
CHIP_SPI_SIZE_KB:=512

board-y=board.o
