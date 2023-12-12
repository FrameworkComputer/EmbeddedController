# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_TEST_PROJECT_REX_ISH app PRIVATE src/board_nb_mode.c)
target_sources_ifdef(CONFIG_TEST_PROJECT_REX_ISH app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/rex/rex-ish/src/board_nb_mode.c)
