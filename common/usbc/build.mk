# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for USB Type-C and Power Delivery

# Note that this variable includes the trailing "/"
_usbc_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

ifneq ($(CONFIG_USB_SM_FRAMEWORK),)
all-obj-$(CONFIG_USB_SM_FRAMEWORK)+=$(_usbc_dir)usb_sm.o
all-obj-$(CONFIG_USB_TYPEC_SM)+=$(_usbc_dir)usbc_task.o
all-obj-$(CONFIG_USB_PRL_SM)+=$(_usbc_dir)usb_prl_sm.o
ifneq ($(CONFIG_USB_PE_SM),)
all-obj-$(CONFIG_USB_TYPEC_VPD)+=$(_usbc_dir)usb_pe_ctvpd_sm.o
all-obj-$(CONFIG_USB_TYPEC_CTVPD)+=$(_usbc_dir)usb_pe_ctvpd_sm.o
all-obj-$(CONFIG_USB_TYPEC_DRP_ACC_TRYSRC)+=$(_usbc_dir)usb_pe_drp_sm.o
endif
all-obj-$(CONFIG_USB_TYPEC_VPD)+=$(_usbc_dir)usb_tc_vpd_sm.o
all-obj-$(CONFIG_USB_TYPEC_CTVPD)+=$(_usbc_dir)usb_tc_ctvpd_sm.o
all-obj-$(CONFIG_USB_TYPEC_DRP_ACC_TRYSRC)+=\
			$(_usbc_dir)usb_tc_drp_acc_trysrc_sm.o
endif
