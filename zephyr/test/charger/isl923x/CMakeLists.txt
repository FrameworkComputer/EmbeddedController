# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if (DEFINED CONFIG_TEST_EXTPOWER)
	target_sources(app PRIVATE src/extpower.c)
	target_sources(app PRIVATE ${PLATFORM_EC_PROGRAM_DIR}/${CONFIG_TEST_SRC})
endif()
