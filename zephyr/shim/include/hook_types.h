/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_HOOK_TYPES_H_
#define __CROS_EC_HOOK_TYPES_H_

#include <zephyr/sys/util_macro.h>

/*
 * Some config macros are defined but without any value for some boards, so
 * IF_ENABLED checks cannot be used. In addition, config values may not be set
 * when generating the linker script: in that case we emit every possible hook
 * type, since doing so won't bloat the output if no corresponding sections
 * were generated.
 */
#if defined(TEST_BUILD) || defined(_LINKER)
#define HOOK_TYPES_TEST_BUILD HOOK_TEST_1, HOOK_TEST_2, HOOK_TEST_3,
#else
#define HOOK_TYPES_TEST_BUILD
#endif

#if defined(CONFIG_USB_SUSPEND) || defined(_LINKER)
#define HOOK_TYPES_USB_SUSPEND HOOK_USB_PM_CHANGE
#else
#define HOOK_TYPES_USB_SUSPEND
#endif

#if defined(CONFIG_BODY_DETECTION) || defined(_LINKER)
#define HOOK_TYPES_BODY_DETECTION HOOK_BODY_DETECT_CHANGE
#else
#define HOOK_TYPES_BODY_DETECTION
#endif

/*
 * HOOK_TYPES_LIST is a sequence of tokens that expands to every enabled
 * `enum hook_type` value.
 *
 * If the enum definition is changed, this macro must also be changed.
 */
#define HOOK_TYPES_LIST                                                     \
	LIST_DROP_EMPTY(                                                    \
		HOOK_INIT, HOOK_PRE_FREQ_CHANGE, HOOK_FREQ_CHANGE,          \
		HOOK_SYSJUMP, HOOK_CHIPSET_PRE_INIT, HOOK_CHIPSET_STARTUP,  \
		HOOK_CHIPSET_RESUME, HOOK_CHIPSET_SUSPEND,                  \
		IF_ENABLED(CONFIG_PLATFORM_EC_CHIPSET_RESUME_INIT_HOOK,     \
			   (HOOK_CHIPSET_RESUME_INIT,                       \
			    HOOK_CHIPSET_SUSPEND_COMPLETE, ))               \
			HOOK_CHIPSET_SHUTDOWN,                              \
		HOOK_CHIPSET_SHUTDOWN_COMPLETE, HOOK_CHIPSET_HARD_OFF,      \
		HOOK_CHIPSET_RESET, HOOK_AC_CHANGE, HOOK_LID_CHANGE,        \
		HOOK_TABLET_MODE_CHANGE, HOOK_TYPES_BODY_DETECTION,         \
		HOOK_BASE_ATTACHED_CHANGE, HOOK_POWER_BUTTON_CHANGE,        \
		HOOK_BATTERY_SOC_CHANGE, HOOK_TYPES_USB_SUSPEND, HOOK_TICK, \
		IF_ENABLED(CONFIG_PLATFORM_EC_HOOK_SECOND, (HOOK_SECOND, )) \
			HOOK_USB_PD_DISCONNECT,                             \
		HOOK_USB_PD_CONNECT, HOOK_POWER_SUPPLY_CHANGE,              \
		HOOK_TYPES_TEST_BUILD)

#endif
