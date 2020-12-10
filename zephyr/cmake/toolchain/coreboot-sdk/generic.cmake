# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# generic.cmake is used for host-side compilation and preprocessing
# (e.g., for device-tree).  Thus, we should use LLVM for this
# actually, as that's what's currently supported compiler-wise in the
# chroot right now.

set(CMAKE_C_COMPILER "/usr/bin/x86_64-pc-linux-gnu-clang")

set(CMAKE_AR         "/usr/bin/llvm-ar")
set(CMAKE_NM         "/usr/bin/llvm-nm")
set(CMAKE_OBJCOPY    "/usr/bin/llvm-objcopy")
set(CMAKE_OBJDUMP    "/usr/bin/llvm-objdump")
set(CMAKE_RANLIB     "/usr/bin/llvm-ar")
set(CMAKE_READELF    "/usr/bin/llvm-readelf")
