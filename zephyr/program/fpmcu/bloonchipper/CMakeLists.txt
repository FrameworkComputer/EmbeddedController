# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(b/325661196): Remove this once bloonchipper builds with this flag.
zephyr_cc_option(-Wno-int-conversion)

zephyr_library_sources("src/power.c")

zephyr_library_sources_ifdef(CONFIG_BLOONCHIPPER_EC_RO_QUIRKS "src/ec_quirks.c")
zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD "src/fp_transport.c")
