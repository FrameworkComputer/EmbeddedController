# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025	fingerprint_fpc1025.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1025_PAL
			     fingerprint_fpc1025_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1145	fingerprint_fpc1145.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_FPC1145_PAL
			     fingerprint_fpc1145_pal.c)
zephyr_library_sources_ifdef(CONFIG_FINGERPRINT_SENSOR_SIMULATOR
			     fingerprint_sensor_sim.c)
