# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Tarkin Makeifile
zephyr_include_directories("include")
zephyr_library_sources("src/board.c")
zephyr_library_sources("src/kb_backlight.c")
zephyr_library_sources("src/board_touchpanel.c")
zephyr_library_sources("src/charger.c")
zephyr_library_sources("src/usbc_runtime.c")
