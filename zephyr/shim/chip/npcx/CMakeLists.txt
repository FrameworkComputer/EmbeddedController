# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# When building code coverage, the final Zephyr image isn't actually linked
# and there's no valid image to program. Skip compiling and linking the NPCX
# monitor when coverage is enabled.
if (NOT DEFINED CONFIG_COVERAGE)
  add_subdirectory(npcx_monitor)
endif()

cros_ec_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_KB_RAW_NPCX  keyboard_raw.c)
if (CONFIG_CROS_SHI_NPCX OR CONFIG_EC_HOST_CMD_BACKEND_SHI_NPCX)
        zephyr_library_sources(shi.c)
endif ()
zephyr_library_sources_ifdef(CONFIG_CROS_EC           system.c)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_EXTERNAL_STORAGE
        system_external_storage.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API
        system_download_from_flash.c)
zephyr_library_sources_ifdef(CONFIG_PM_POLICY_CUSTOM  power_policy.c)
