# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("${ZEPHYR_BASE}/arch/arm/include")

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_IRQ irq_command.c)
zephyr_library_sources_ifdef(CONFIG_MPU mpu.c)
zephyr_library_sources(panic.c)
