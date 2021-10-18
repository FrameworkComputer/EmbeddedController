# -*- makefile -*-
# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Primus board specific files build
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
board-y+=ps2.o
board-y+=sensors.o
board-y+=thermal.o
board-y+=usbc_config.o
