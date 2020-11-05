# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

string(TOLOWER "i2c-${CHIP_FAMILY}.c" chip_specific_i2c_shim)
zephyr_sources(${chip_specific_i2c_shim})
zephyr_sources(clock.c)
zephyr_sources(keyboard_raw.c)
