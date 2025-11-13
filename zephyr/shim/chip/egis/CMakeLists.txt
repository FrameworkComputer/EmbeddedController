# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cros_ec_library_include_directories(include)

zephyr_library_sources(clock.c)
zephyr_library_sources(mpu.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC  system.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ET171_ENTER_BOOTLOADER enter_bootloader.c)
zephyr_library_sources_ifdef(CONFIG_EGIS_ET171_OTP_KEY otp_key_et171.c)

if(CONFIG_EGIS_ET171_OTP_KEY)
	zephyr_library_link_libraries(ET171HAL)
endif()
