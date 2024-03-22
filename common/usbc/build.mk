# Copyright 2019 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Build for USB Type-C and Power Delivery

ifneq ($(CONFIG_USB_PD_TCPMV2),)
common-usbc-$(CONFIG_USB_PD_TCPMV2) += usb_pd_timer.o usb_sm.o usbc_task.o

# Type-C state machines
ifneq ($(CONFIG_USB_TYPEC_SM),)
common-usbc-$(CONFIG_USB_VPD) += usb_tc_vpd_sm.o
common-usbc-$(CONFIG_USB_CTVPD) += usb_tc_ctvpd_sm.o
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usb_tc_drp_acc_trysrc_sm.o
endif # CONFIG_USB_TYPEC_SM

# Protocol state machine
ifneq ($(CONFIG_USB_PRL_SM),)
common-usbc-$(CONFIG_USB_PD_TCPMV2) += usb_prl_sm.o
endif # CONFIG_USB_PRL_SM

# Policy Engine state machines
ifneq ($(CONFIG_USB_PE_SM),)
common-usbc-$(CONFIG_USB_VPD) += usb_pe_ctvpd_sm.o
common-usbc-$(CONFIG_USB_CTVPD) += usb_pe_ctvpd_sm.o
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usbc_pd_policy.o
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usb_pe_drp_sm.o
common-usbc-$(CONFIG_USB_DRP_ACC_TRYSRC) += usb_pd_dpm.o
common-usbc-$(CONFIG_USB_PD_VDM_AP_CONTROL) += ap_vdm_control.o
common-usbc-$(CONFIG_USB_PD_DP_MODE) += dp_alt_mode.o
common-usbc-$(CONFIG_USB_PD_DP_HPD_GPIO) += dp_hpd_gpio.o
common-usbc-$(CONFIG_USB_PD_TBT_COMPAT_MODE) += tbt_alt_mode.o
common-usbc-$(CONFIG_USB_PD_USB4) += usb_mode.o
common-usbc-$(CONFIG_CMD_PD) += usb_pd_console.o
common-usbc-$(CONFIG_USB_PD_HOST_CMD) += usb_pd_host.o
endif # CONFIG_USB_PE_SM

# Retimer firmware update
common-usbc-$(CONFIG_USBC_RETIMER_FW_UPDATE) += usb_retimer_fw_update.o

# ALT-DP mode for UFP ports
common-usbc-$(CONFIG_USB_PD_ALT_MODE_UFP_DP) += usb_pd_dp_ufp.o
endif # CONFIG_USB_PD_TCPMV2

# For testing
common-usbc-$(CONFIG_TEST_USB_PD_TIMER) += usb_pd_timer.o
common-usbc-$(CONFIG_TEST_USB_PE_SM) += usbc_pd_policy.o usb_pe_drp_sm.o
common-usbc-$(CONFIG_TEST_SM) += usb_sm.o
