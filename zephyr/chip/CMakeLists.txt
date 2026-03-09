# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# When building code coverage, the final Zephyr image isn't actually linked
# and there's no valid image to program. Skip compiling and linking the
# NPCX/RTK monitor when coverage is enabled.
if (NOT DEFINED CONFIG_COVERAGE)
  add_subdirectory_ifdef(CONFIG_MCHP_SECOND_LOADER_BINARY mchp_second_loader)
  add_subdirectory_ifdef(CONFIG_NPCX_MONITOR_BINARY npcx_monitor)
  add_subdirectory_ifdef(CONFIG_RTK_FLAME_BINARY rtk_flame)
endif()
