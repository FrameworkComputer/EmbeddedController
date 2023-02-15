# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_TEST_BOARD_USB_PD_POLICY app PRIVATE src/usb_pd_policy.c ${PLATFORM_EC_PROGRAM_DIR}/skyrim/src/usb_pd_policy.c)