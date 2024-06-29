# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ACCEL_LIS2DW12 "src/board.c")
