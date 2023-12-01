# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC           system.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_ITE   keyboard_raw.c)
zephyr_library_sources_ifdef(CONFIG_MPU               mpu.c)
zephyr_library_sources_ifdef(CONFIG_PM_POLICY_CUSTOM  power_policy.c)
if (CONFIG_CROS_SHI_IT8XXX2 OR CONFIG_EC_HOST_CMD_BACKEND_SHI_ITE)
	zephyr_library_sources(shi.c)
endif ()
