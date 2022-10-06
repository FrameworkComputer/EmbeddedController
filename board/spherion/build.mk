# -*- makefile -*-
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is ITE IT8xxx2
CHIP:=it83xx
CHIP_FAMILY:=it8xxx2
CHIP_VARIANT:=it81202bx_1024
BASEBOARD:=asurada

board-y+=battery.o board.o led.o
