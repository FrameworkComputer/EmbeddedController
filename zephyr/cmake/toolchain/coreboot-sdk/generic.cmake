# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# generic.cmake is used for host-side compilation and preprocessing
# (e.g., for device-tree).  Thus, we should use LLVM for this
# actually, as that's what's currently supported compiler-wise in the
# chroot right now.
include("${TOOLCHAIN_ROOT}/cmake/toolchain/llvm/generic.cmake")
