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
baseboard-$(CONFIG_USB_PD_TCPM_ITE_ON_CHIP)+=ite_ec.o

#BC1.2 specific files
baseboard-$(CONFIG_BC12_DETECT_MAX14637)+=bc12.o

#USB MUX specific files
baseboard-$(CONFIG_USB_MUX_VIRTUAL)+=usb_mux.o
baseboard-$(CONFIG_USB_MUX_ANX7440)+=usb_mux.o

#USB Retimer specific files
baseboard-$(CONFIG_USBC_RETIMER_INTEL_BB)+=retimer.o
