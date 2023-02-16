# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/common.c)

# PPC config
if (DEFINED CONFIG_TEST_BOARD_PPC_CONFIG)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/skyrim/${CONFIG_TEST_BOARD_NAME}/src/ppc_config.c)
	if (NOT DEFINED CONFIG_TEST_BOARD_PPC_CONFIG_CUSTOM)
		target_sources(app PRIVATE src/ppc_config.c)
	endif()
endif()