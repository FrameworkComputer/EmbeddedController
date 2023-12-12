# SPDX-License-Identifier: Apache-2.0

zephyr_library_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_USB_PD_ALTMODE_INTEL
	pd_task_intel_altmode.c)
