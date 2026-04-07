# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_EC_CBI_UFSC_KB_BACKLIGHT keyboard_backlight.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC_CBI_UFSC_PDC pdc_selector.c)
