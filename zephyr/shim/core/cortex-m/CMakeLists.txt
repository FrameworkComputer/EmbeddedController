# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_IRQ irq_command.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MPU mpu.c)
