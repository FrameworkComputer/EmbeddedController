# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/compiler_flags.cmake")

# Disable -fno-freestanding.
set_compiler_property(PROPERTY hosted)

check_set_compiler_property(APPEND PROPERTY warning_extended -Wunused-variable
	-Werror=unused-variable -Werror=missing-braces
	-Werror=sometimes-uninitialized -Werror=unused-function
	-Werror=array-bounds)
