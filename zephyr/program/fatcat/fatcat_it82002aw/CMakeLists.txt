# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_include_directories("../../intelrvp/include")

zephyr_library_sources("../../intelrvp/ptlrvp/src/board_power.c")

zephyr_library_sources("../../intelrvp/src/dc_jack.c")
zephyr_library_sources("../../intelrvp/src/kbd_strauss.c")

zephyr_library_sources("src/hibernate.c")
