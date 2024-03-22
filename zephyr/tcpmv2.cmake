# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TBT_COMPAT_MODE
                                                "${PLATFORM_EC}/common/usbc/tbt_alt_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_USB4
                                                "${PLATFORM_EC}/common/usbc/usb_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_DP_MODE
                                                "${PLATFORM_EC}/common/usbc/dp_alt_mode.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_VPD
                                                "${PLATFORM_EC}/common/usbc/usb_tc_vpd_sm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_CTVPD
                                                "${PLATFORM_EC}/common/usbc/usb_tc_ctvpd_sm.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_DRP_ACC_TRYSRC
                                                "${PLATFORM_EC}/common/usbc/usb_tc_drp_acc_trysrc_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pe_drp_sm.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_dpm.c"
                                                "${PLATFORM_EC}/common/usbc/usbc_pd_policy.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_FW_UPDATE
                                                "${PLATFORM_EC}/common/usbc/usb_retimer_fw_update.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_CONSOLE_CMD
                                                "${PLATFORM_EC}/common/usb_pd_console_cmd.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_HOST_CMD
                                                "${PLATFORM_EC}/common/usb_pd_host_cmd.c"
                                                "${PLATFORM_EC}/common/usbc/usb_pd_host.c")
