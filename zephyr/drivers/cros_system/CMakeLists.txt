# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_IT8XXX2	cros_system_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_NPCX	cros_system_npcx.c)
