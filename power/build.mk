# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Power management for application processor and peripherals
#

power-$(CONFIG_CHIPSET_APL_GLK)+=apollolake.o intel_x86.o
power-$(CONFIG_CHIPSET_BRASWELL)+=braswell.o
power-$(CONFIG_CHIPSET_CANNONLAKE)+=cannonlake.o intel_x86.o
power-$(CONFIG_CHIPSET_ECDRIVEN)+=ec_driven.o
power-$(CONFIG_CHIPSET_MEDIATEK)+=mediatek.o
power-$(CONFIG_CHIPSET_RK3399)+=rk3399.o
power-$(CONFIG_CHIPSET_ROCKCHIP)+=rockchip.o
power-$(CONFIG_CHIPSET_SDM845)+=sdm845.o
power-$(CONFIG_CHIPSET_SKYLAKE)+=skylake.o intel_x86.o
power-$(CONFIG_CHIPSET_STONEY)+=stoney.o
power-$(CONFIG_POWER_COMMON)+=common.o
