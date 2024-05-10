# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources_ifdef(CONFIG_TEST debug.c)
