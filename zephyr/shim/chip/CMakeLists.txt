# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Translate Zephyr CONFIG_SOC_* values to platform/ec CHIP_* values.
if (DEFINED CONFIG_SOC_NPCX7M6FB)
	set(CHIP_FAMILY "NPCX7")
	set(CHIP_VARIANT "NPCX7M6FB")
endif()

# Add compile definitions for CHIP_FAMILY_ and CHIP_VARIANT_, these are used
# through the platform/ec code and are often used to branch header definitions
# which are still being used. It is possible that eventually (when we also no
# longer require the platform/ec/chip/... headers) that we'll be able to get
# rid of this.
zephyr_compile_definitions("CHIP_FAMILY_${CHIP_FAMILY}")
zephyr_compile_definitions("CHIP_VARIANT_${CHIP_VARIANT}")

if ("${CHIP_FAMILY}" MATCHES "^NPCX[0-9]$")
	add_subdirectory(npcx)
	zephyr_include_directories("${PLATFORM_EC}/chip/npcx")
	string(TOLOWER "npcx/i2c-${CHIP_FAMILY}.c" chip_specific_i2c_impl)
endif()

# Adding chip specific i2c implementation here. These contain the specific
# code used to implement generic information about platform/ec
# port numbers and their properties. As such they're basically the
# platform/ec's abstraction of devicetree and aren't worth replacing
# quite yet until a cleaner picture of how the full migration for i2c
# will be done.
if (DEFINED chip_specific_i2c_impl)
	zephyr_sources_ifdef(CONFIG_PLATFORM_EC_I2C
		"${PLATFORM_EC}/chip/${chip_specific_i2c_impl}")
endif()

