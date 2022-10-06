# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include("${ZEPHYR_BASE}/cmake/compiler/clang/compiler_flags.cmake")

# Disable -fno-freestanding.
set_compiler_property(PROPERTY hosted)

# Disable position independent code.
add_compile_options(-fno-PIC)

check_set_compiler_property(APPEND PROPERTY warning_extended -Wunused-variable
	-Werror=unused-variable -Werror=missing-braces
	-Werror=sometimes-uninitialized -Werror=unused-function
	-Werror=array-bounds)
