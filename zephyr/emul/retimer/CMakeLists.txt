# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_EMUL_ANX7483 emul_anx7483.c)
zephyr_library_sources_ifdef(CONFIG_EMUL_PS8811 emul_ps8811.c)
