# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Padme Makeifile
zephyr_library_sources("src/board.c" "src/dp_alt_mode.c" "src/rt9490_workaround.c"
			"src/sensor.c" "src/thermal.c")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_DETACHABLE_BASE
			"src/base_detect.c")
zephyr_library_sources_ifdef(
    CONFIG_PDC_RUNTIME_PORT_CONFIG "src/usbc_runtime.c")
