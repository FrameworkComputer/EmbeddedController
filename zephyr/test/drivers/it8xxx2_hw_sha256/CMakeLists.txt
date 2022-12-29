# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${PLATFORM_EC}/zephyr/shim/chip/it8xxx2/include")
zephyr_library_sources("${PLATFORM_EC}/driver/sha256/sha256_it8xxx2.c")
# Include the test source and the file under test
target_sources(app PRIVATE src/main.c)
