# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# R2D2 Makeifile
zephyr_include_directories("include")
zephyr_library_sources("src/dp_alt_mode.c")
zephyr_library_sources("src/pen_charge.c")
