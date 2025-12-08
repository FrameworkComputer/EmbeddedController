# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources("src/platform.c")
zephyr_library_sources("src/flash_qspi.c")

if(DEFINED CONFIG_CROS_EC_RO)

set(flash_header_output_bin ${CMAKE_BINARY_DIR}/flash_header.bin)
zephyr_file_copy(flash_header.bin ${flash_header_output_bin})

endif()
