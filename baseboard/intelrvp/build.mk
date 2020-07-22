# -*- makefile -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Baseboard specific files build
#

#Intel RVP common files
baseboard-y=baseboard.o
baseboard-$(CONFIG_LED_COMMON)+=led.o led_states.o
baseboard-$(CONFIG_BATTERY_SMART)+=battery.o
baseboard-$(CONFIG_USB_POWER_DELIVERY)+=chg_usb_pd.o usb_pd_policy.o

#EC specific files
baseboard-$(VARIANT_INTELRVP_EC_IT8320)+=ite_ec.o

#BC1.2 specific files
baseboard-$(CONFIG_BC12_DETECT_MAX14637)+=bc12.o
