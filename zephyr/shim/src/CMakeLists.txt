# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_sources(console.c)
zephyr_sources_ifdef(CONFIG_PLATFORM_EC_TIMER hwtimer.c)
