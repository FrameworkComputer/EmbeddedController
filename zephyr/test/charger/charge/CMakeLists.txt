# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/extpower.c)

target_sources_ifdef(CONFIG_TEST_CHARGE_ACOKREF app PRIVATE src/charge_acokref.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_MANAGER app PRIVATE src/charge_manager.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_RAMP app PRIVATE src/charge_ramp.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_STATE app PRIVATE src/charge_state.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_STATE_PREVENT_POWER_ON app PRIVATE src/charge_state_prevent_power_on.c)
target_sources_ifdef(CONFIG_TEST_CHARGE_MANAGER_SEEDING app PRIVATE
  src/charge_manager_seeding.c)
