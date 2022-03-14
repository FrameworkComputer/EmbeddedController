# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

register_host_test(
    "math_fixed", kconfig_files=[here / "prj.conf", here / "fixed_point.conf"]
)
# TODO (b/224582527) uncomment when FPU is enabled in POSIX arch
# register_host_test("math_float",
#                    kconfig_files=[here / "prj.conf",
#                                   here / "floating_point.conf"])
