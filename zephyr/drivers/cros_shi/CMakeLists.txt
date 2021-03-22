# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_SHI_IT8XXX2	cros_shi_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SHI_NPCX	cros_shi_npcx.c)
