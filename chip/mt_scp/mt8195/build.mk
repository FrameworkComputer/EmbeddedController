# -*- makefile -*-
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Required chip modules
chip-y+=$(CHIP_VARIANT)/clock.o
chip-y+=$(CHIP_VARIANT)/video.o
chip-y+=$(CHIP_VARIANT)/intc_group.o
chip-y+=$(CHIP_VARIANT)/uart.o

ifeq ($(BOARD),cherry_scp)
chip-y+=$(CHIP_VARIANT)/video.o
endif
