# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F072B
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f07x

board-y=board.o usb_pd_policy.o injector.o simpletrace.o usb_pd_pdo.o
board-$(HAS_TASK_SNIFFER)+=sniffer.o
