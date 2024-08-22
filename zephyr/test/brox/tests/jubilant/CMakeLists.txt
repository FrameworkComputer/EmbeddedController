# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC_PROGRAM_DIR}/brox/jubilant/include")

target_sources_ifdef(CONFIG_TEST_BOARD_JUBILANT_KEYBOARD app PRIVATE src/keyboard.c)
target_sources_ifdef(CONFIG_TEST_BOARD_JUBILANT_KEYBOARD app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/brox/jubilant/src/keyboard_customization.c)
