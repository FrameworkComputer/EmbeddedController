# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_NPCX	cros_kb_raw_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_ITE	cros_kb_raw_ite.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_XEC	cros_kb_raw_xec.c)
