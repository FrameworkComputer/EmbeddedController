# -*- makefile -*-
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx7
CHIP_VARIANT:=npcx7m7wc
BASEBOARD:=zork

board-y=board.o led.o
board-$(CONFIG_BATTERY_SMART)+=battery.o
