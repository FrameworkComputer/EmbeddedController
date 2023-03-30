# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Add source files
target_sources(app PRIVATE
  src/shim_battery_wip.c
  src/test_upstream_fuel_gauge.c
)
