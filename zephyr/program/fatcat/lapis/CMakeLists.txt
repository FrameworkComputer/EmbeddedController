# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources("src/board_power.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR_APL6012
                                                "${PLATFORM_EC}/driver/temp_sensor/thermistor.c")
