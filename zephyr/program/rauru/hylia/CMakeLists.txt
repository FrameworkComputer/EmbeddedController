# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a
# BSD-style license that can be found in the LICENSE file.

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/dp_alt_mode.c)
zephyr_library_sources("src/kb_backlight.c" "src/board.c" "src/fan.c"
"src/thermal.c" "src/backlight.c")
