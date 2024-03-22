# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories(include)
zephyr_library_sources(ec_app_main.c)
if(NOT DEFINED CONFIG_TEST)
    zephyr_library_sources(main_shim.c)
endif()

add_subdirectory(chip)
