# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources_ifdef(CONFIG_TEST_CHARGE_MANAGER app PRIVATE src/charge_manager.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_RAMP app PRIVATE src/charge_ramp.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_STATE_PREVENT_POWER_ON app PRIVATE src/charge_state_prevent_power_on.c)
