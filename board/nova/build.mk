# -*- makefile -*-
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Brask board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m3f
BASEBOARD:=brask

board-y=
board-y+=board.o
board-y+=fans.o
board-y+=fw_config.o
board-y+=i2c.o
board-y+=led.o
board-y+=pwm.o
board-y+=sensors.o
board-y+=usbc_config.o
