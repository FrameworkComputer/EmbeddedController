# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_interface_library_named(test_utils)

zephyr_library_sources(fff.c)

zephyr_library_link_libraries(test_utils)
