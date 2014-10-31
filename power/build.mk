# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Power management for application processor and peripherals
#

power-$(CONFIG_CHIPSET_BAYTRAIL)+=baytrail.o
power-$(CONFIG_CHIPSET_ECDRIVEN)+=ec_driven.o
power-$(CONFIG_CHIPSET_GAIA)+=gaia.o
power-$(CONFIG_CHIPSET_HASWELL)+=haswell.o
power-$(CONFIG_CHIPSET_IVYBRIDGE)+=ivybridge.o
power-$(CONFIG_CHIPSET_ROCKCHIP)+=rockchip.o
power-$(CONFIG_CHIPSET_TEGRA)+=tegra.o
power-$(CONFIG_CHIPSET_BRASWELL)+=braswell.o
power-$(CONFIG_POWER_COMMON)+=common.o
