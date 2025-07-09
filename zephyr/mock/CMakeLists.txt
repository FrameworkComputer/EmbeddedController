# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_POWER_SEQUENCE_MOCK power.c)
zephyr_library_sources_ifdef(CONFIG_AP_POWER_EVENTS_MOCK ap_power_events.c)
zephyr_library_sources_ifdef(CONFIG_POWER_SIGNALS_MOCK power_signals.c)
zephyr_library_sources_ifdef(CONFIG_CHIPSET_API_MOCK chipset_api.c)
zephyr_library_sources_ifdef(CONFIG_ISL923X_MOCK isl923x.c)

cros_ec_library_include_directories(include)

if(DEFINED CONFIG_POWER_SIGNALS_MOCK)
  zephyr_include_directories("${PLATFORM_EC}/zephyr/subsys/ap_pwrseq/include")
endif()
