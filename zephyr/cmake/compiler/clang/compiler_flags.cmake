# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/compiler_flags.cmake")

# Disable -fno-freestanding.
set_compiler_property(PROPERTY hosted)
