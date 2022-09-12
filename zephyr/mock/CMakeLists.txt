# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_POWER_SEQUENCE_MOCK power.c)

cros_ec_library_include_directories(include)
