# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_ADSP_COMMS adsp_i2c_mode.c)
zephyr_library_sources_ifdef(CONFIG_ADSP_COMMS adsp_comms.c)
