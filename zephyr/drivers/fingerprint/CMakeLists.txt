# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025	fpc1025.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025_PAL
			     fpc1025_pal.c)
