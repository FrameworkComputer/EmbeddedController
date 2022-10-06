# -*- makefile -*-
# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

# Select eMMC CMD0 driver.
EMMC_CMD0_DRIVER=$(if $(CHIP_IT83XX),emmc_ite.o,emmc.o)

baseboard-y=baseboard.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=usb_pd_policy.o
baseboard-$(CONFIG_BOOTBLOCK)+=$(EMMC_CMD0_DRIVER)

baseboard-$(VARIANT_KUKUI_BATTERY_MAX17055)+=battery_max17055.o
baseboard-$(VARIANT_KUKUI_BATTERY_MM8013)+=battery_mm8013.o
baseboard-$(VARIANT_KUKUI_BATTERY_BQ27541)+=battery_bq27541.o
baseboard-$(VARIANT_KUKUI_BATTERY_SMART)+=battery_smart.o

baseboard-$(VARIANT_KUKUI_CHARGER_MT6370)+=charger_mt6370.o

baseboard-$(VARIANT_KUKUI_POGO_KEYBOARD)+=base_detect_kukui.o

$(out)/RO/baseboard/$(BASEBOARD)/$(EMMC_CMD0_DRIVER): $(out)/bootblock_data.h

# bootblock size from 12769.0
DEFAULT_BOOTBLOCK_SIZE:=21504
