# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Vader Makefile
zephyr_library_sources_ifdef(
    CONFIG_PDC_RUNTIME_PORT_CONFIG "src/usbc_runtime.c")
zephyr_include_directories("include")
zephyr_library_sources("src/sensor.c")
zephyr_library_sources("src/touch_pad.c")
