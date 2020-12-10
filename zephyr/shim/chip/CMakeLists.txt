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
endif()

