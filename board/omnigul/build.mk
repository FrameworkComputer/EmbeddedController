# -*- makefile -*-
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Brya board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m3f
BASEBOARD:=brya

board-y=
board-y+=battery.o
board-y+=board.o
board-y+=charger.o
board-y+=fans.o
board-y+=fw_config.o
board-y+=i2c.o
board-y+=keyboard.o
board-y+=led.o
board-y+=pwm.o
board-y+=sensors.o
board-y+=usbc_config.o
board-y+=keyboard_customization.o
