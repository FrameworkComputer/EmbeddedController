# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(
  CONFIG_LINK_TEST_SUITE_SMART_BATTERY
  app
  PRIVATE
  src/smart.c
)

target_sources_ifdef(
  CONFIG_LINK_TEST_SUITE_SMART_BATTERY_PRESENT
  app
  PRIVATE
  src/battery_present.c
)
