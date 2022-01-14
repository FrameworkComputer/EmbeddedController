# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_RTC_NPCX	cros_rtc_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_RTC_NXP_PCF85063A
	nxp_rtc_pcf85063a.c)
zephyr_library_sources_ifdef(CONFIG_CROS_RTC_RENESAS_IDT1337AG
	renesas_rtc_idt1337ag.c)
zephyr_library_sources_ifdef(CONFIG_CROS_RTC_XEC	cros_rtc_xec.c)
