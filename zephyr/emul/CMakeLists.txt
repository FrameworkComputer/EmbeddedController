# Copyright 2021 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_EMUL_SMART_BATTERY	emul_smart_battery.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_BMA255		emul_bma255.c)
