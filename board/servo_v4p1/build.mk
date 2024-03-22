# -*- makefile -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F072RBT6
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f07x

# Not enough SRAM: Disable all tests
test-list-y=

# These files are compiled into RO and RW
board-y=board.o
board-y+=ioexpanders.o
board-y+=dacs.o
board-y+=pi3usb9201.o

# These files are compiled into RO only
board-ro+=ccd_measure_sbu.o
board-ro+=pathsel.o
board-ro+=chg_control.o
board-ro+=ina231s.o
board-ro+=usb_pd_policy.o
board-ro+=fusb302b.o
board-ro+=usb_sm.o
board-ro+=usb_tc_snk_sm.o
board-ro+=poweron_conf.o

all_deps=$(patsubst ro,,$(def_all_deps))
