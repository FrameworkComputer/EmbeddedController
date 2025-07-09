# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_RTC_NPCX	cros_rtc_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_RTC_XEC	cros_rtc_xec.c)
