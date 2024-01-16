# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# When building code coverage, the final Zephyr image isn't actually linked
# and there's no valid image to program. Skip compiling and linking the mchp
# second loader when coverage is enabled.
if (NOT DEFINED CONFIG_COVERAGE)
  add_subdirectory(second_loader)
endif()

cros_ec_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_MCHP  keyboard_raw.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC           system.c)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE
        system_external_storage.c
        system_download_from_flash.c)
