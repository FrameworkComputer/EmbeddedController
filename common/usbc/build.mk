# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for USB Type-C and Power Delivery

# Note that this variable includes the trailing "/"
_usbc_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

ifneq ($(CONFIG_USB_PD_TCPMV2),)
all-obj-y+=$(_usbc_dir)usb_sm.o
all-obj-y+=$(_usbc_dir)usbc_task.o

# Type-C state machines
ifneq ($(CONFIG_USB_TYPEC_SM),)
all-obj-$(CONFIG_USB_VPD)+=$(_usbc_dir)usb_tc_vpd_sm.o
all-obj-$(CONFIG_USB_CTVPD)+=$(_usbc_dir)usb_tc_ctvpd_sm.o
all-obj-$(CONFIG_USB_DRP_ACC_TRYSRC)+=$(_usbc_dir)usb_tc_drp_acc_trysrc_sm.o
endif # CONFIG_USB_TYPEC_SM

# Protocol state machine
ifneq ($(CONFIG_USB_PRL_SM),)
all-obj-y+=$(_usbc_dir)usb_prl_sm.o
endif # CONFIG_USB_PRL_SM

# Policy Engine state machines
ifneq ($(CONFIG_USB_PE_SM),)
all-obj-$(CONFIG_USB_VPD)+=$(_usbc_dir)usb_pe_ctvpd_sm.o
all-obj-$(CONFIG_USB_CTVPD)+=$(_usbc_dir)usb_pe_ctvpd_sm.o
all-obj-$(CONFIG_USB_DRP_ACC_TRYSRC)+=$(_usbc_dir)usb_pe_drp_sm.o
all-obj-$(CONFIG_USB_DRP_ACC_TRYSRC)+=$(_usbc_dir)usb_pd_dpm.o
all-obj-$(CONFIG_USB_DRP_ACC_TRYSRC)+=$(_usbc_dir)dp_alt_mode.o
all-obj-$(CONFIG_USB_PD_TBT_COMPAT_MODE)+=$(_usbc_dir)tbt_alt_mode.o
all-obj-$(CONFIG_USB_PD_USB4)+=$(_usbc_dir)usb_mode.o
all-obj-$(CONFIG_CMD_PD)+=$(_usbc_dir)usb_pd_console.o
all-obj-$(CONFIG_USB_PD_HOST_CMD)+=$(_usbc_dir)usb_pd_host.o
endif # CONFIG_USB_PE_SM

endif # CONFIG_USB_PD_TCPMV2

# For testing
all-obj-$(CONFIG_TEST_USB_PE_SM)+=$(_usbc_dir)usb_pe_drp_sm.o
all-obj-$(CONFIG_TEST_SM)+=$(_usbc_dir)usb_sm.o
