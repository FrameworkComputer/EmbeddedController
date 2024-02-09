# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_include_directories(${PLATFORM_EC}/extra/um_ppm)

zephyr_library_sources(ppm_driver.c)
zephyr_library_sources(ucsi_host_cmd.c)
zephyr_library_sources(platform_zephyr.c)
# This should move to zephyr/subsys/ucsi (b/339702957).
zephyr_library_sources(${PLATFORM_EC}/extra/um_ppm/ppm_common.c)
