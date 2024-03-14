# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/common.c)

if (DEFINED CONFIG_TEST_BOARD_USBC)
	target_sources(app PRIVATE src/usbc.c)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/brox/src/usbc.c)
endif()

if (DEFINED CONFIG_TEST_BOARD_HIBERNATE)
  target_compile_definitions(app PRIVATE CHARGER_SOLO=0)
  target_sources(app PRIVATE src/hibernate.c)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/brox/src/hibernate.c)
endif()

if (DEFINED CONFIG_TEST_BOARD_SUSPEND_RESUME)
	target_sources(app PRIVATE src/suspend_resume.c)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/brox/src/suspend_resume.c)
endif()
