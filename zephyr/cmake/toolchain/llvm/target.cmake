# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set(COMPILER clang)
set(LINKER lld)
set(BINTOOLS llvm)

# Mapping of Zephyr architecture -> toolchain triple
# Note only "posix" is supported at the moment.
set(CROSS_COMPILE_TARGET_posix    x86_64-pc-linux-gnu)

set(CROSS_COMPILE_TARGET          ${CROSS_COMPILE_TARGET_${ARCH}})

set(CC clang)
set(CROSS_COMPILE "/usr/bin/${CROSS_COMPILE_TARGET}-")
