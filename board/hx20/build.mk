# -*- makefile -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build
#

# the IC is Microchip MEC1701
# external SPI is 512KB
# external clock is crystal
CHIP:=mchp

CHIP_FAMILY:=mec152x
# MEC1521 144 WFBGA
CHIP_VARIANT:=mec152x_3400

CHIP_SPI_SIZE_KB:=512

BASEBOARD:=fwk

board-y=board.o led.o power_sequence.o cypress5525.o ucsi.o cpu_power.o
board-$(CONFIG_KEYBOARD_CUSTOMIZATION)+=keyboard_customization.o
board-$(CONFIG_POWER_BUTTON_CUSTOM) += power_button_x86.o
board-$(CONFIG_PECI) += peci_customization.o
board-$(HAS_TASK_HOSTCMD) += host_command_customization.o
board-$(CONFIG_I2C_HID_MEDIAKEYS) += i2c_hid_mediakeys.o



