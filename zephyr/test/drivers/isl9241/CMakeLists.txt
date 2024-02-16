# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(b/325661196): Remove this once this target builds with this flag.
zephyr_cc_option(-Wno-int-conversion)

target_sources(app PRIVATE src/isl9241.c)
