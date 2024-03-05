# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources_ifdef(CONFIG_PLATFORM_EC_CONSOLE_CMD_CHARGER_ADC_AMON_BMON
			app PRIVATE src/console_cmd_amon_bmon.c)
target_sources_ifdef(CONFIG_PLATFORM_EC_CHARGE_RAMP_HW
			app PRIVATE src/charge_ramp_hw.c)
target_sources_ifdef(CONFIG_CHARGER_ISL9238C app PRIVATE src/isl9238c.c)
