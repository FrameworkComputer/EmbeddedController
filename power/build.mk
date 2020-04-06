# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Power management for application processor and peripherals
#

power-$(CONFIG_CHIPSET_APL_GLK)+=apollolake.o intel_x86.o
power-$(CONFIG_CHIPSET_BRASWELL)+=braswell.o
power-$(CONFIG_CHIPSET_CANNONLAKE)+=cannonlake.o intel_x86.o
power-$(CONFIG_CHIPSET_COMETLAKE)+=cometlake.o intel_x86.o
power-$(CONFIG_CHIPSET_COMETLAKE_DISCRETE)+=cometlake-discrete.o intel_x86.o
power-$(CONFIG_CHIPSET_ECDRIVEN)+=ec_driven.o
power-$(CONFIG_CHIPSET_ICELAKE)+=icelake.o intel_x86.o
power-$(CONFIG_CHIPSET_MT817X)+=mt817x.o
power-$(CONFIG_CHIPSET_MT8183)+=mt8183.o
power-$(CONFIG_CHIPSET_MT8192)+=mt8192.o
power-$(CONFIG_CHIPSET_RK3288)+=rk3288.o
power-$(CONFIG_CHIPSET_RK3399)+=rk3399.o
power-$(CONFIG_CHIPSET_SC7180)+=sc7180.o
power-$(CONFIG_CHIPSET_SDM845)+=sdm845.o
power-$(CONFIG_CHIPSET_SKYLAKE)+=skylake.o intel_x86.o
power-$(CONFIG_CHIPSET_STONEY)+=stoney.o
power-$(CONFIG_POWER_COMMON)+=common.o
power-$(CONFIG_POWER_TRACK_HOST_SLEEP_STATE)+=host_sleep.o
