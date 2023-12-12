# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_USBC_INTEL_ALTMODE intel_altmode.c)
zephyr_library_sources_ifdef(CONFIG_USBC_PDC_RTS545X pdc_rts545x.c)
