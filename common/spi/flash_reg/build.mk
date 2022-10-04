# -*- makefile -*-
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

SELF_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
REL_PATH := $(shell realpath --relative-to $(_common_dir) $(SELF_DIR))

includes+=$(REL_PATH)public
common-$(CONFIG_SPI_FLASH)+=$(REL_PATH)/src/spi_flash_reg.o
common-$(CONFIG_SPI_FLASH_REGS)+=$(REL_PATH)/src/spi_flash_reg.o
