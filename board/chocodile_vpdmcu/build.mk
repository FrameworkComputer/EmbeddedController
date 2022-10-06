# -*- makefile -*-
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Board specific files build

# the IC is STmicro STM32F051K8U6TR
CHIP:=stm32
CHIP_FAMILY:=stm32f0
CHIP_VARIANT:=stm32f05x

board-y=board.o vpd_api.o
#
# This target builds RW only.  Therefore, remove RO from dependencies.
all_deps=$(patsubst ro,,$(def_all_deps))
