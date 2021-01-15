# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_sources(clock.c)
zephyr_sources_ifdef(CONFIG_CROS_KB_RAW_NPCX  keyboard_raw.c)
zephyr_sources_ifdef(CONFIG_CROS_EC           system.c)
