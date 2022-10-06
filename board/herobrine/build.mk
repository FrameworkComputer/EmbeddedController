# -*- makefile -*-
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

CHIP:=npcx
CHIP_FAMILY:=npcx9
CHIP_VARIANT:=npcx9m3f
BASEBOARD:=herobrine

board-y+=battery.o
board-y+=board.o
board-y+=led.o
board-y+=switchcap.o
board-y+=usbc_config.o
