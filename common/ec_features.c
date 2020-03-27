/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Present Chrome EC device features to the outside world */

#include "common.h"
#include "config.h"
#include "console.h"
#include "ec_commands.h"
#include "board_config.h"

uint32_t get_feature_flags0(void)
{
	uint32_t result = 0
#ifdef CONFIG_FW_LIMITED_IMAGE
		| EC_FEATURE_MASK_0(EC_FEATURE_LIMITED)
#endif
#ifdef CONFIG_FLASH
		| EC_FEATURE_MASK_0(EC_FEATURE_FLASH)
#endif
#ifdef CONFIG_FANS
		| EC_FEATURE_MASK_0(EC_FEATURE_PWM_FAN)
#endif
#ifdef CONFIG_KEYBOARD_BACKLIGHT
		| EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB)
#endif
#ifdef HAS_TASK_LIGHTBAR
		| EC_FEATURE_MASK_0(EC_FEATURE_LIGHTBAR)
#endif
#ifdef CONFIG_LED_COMMON
		| EC_FEATURE_MASK_0(EC_FEATURE_LED)
#endif
#ifdef HAS_TASK_MOTIONSENSE
		| EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE)
#endif
#ifdef HAS_TASK_KEYSCAN
		| EC_FEATURE_MASK_0(EC_FEATURE_KEYB)
#endif
#ifdef CONFIG_PSTORE
		| EC_FEATURE_MASK_0(EC_FEATURE_PSTORE)
#endif
#ifdef CONFIG_HOSTCMD_X86
		| EC_FEATURE_MASK_0(EC_FEATURE_PORT80)
#endif
#ifdef CONFIG_TEMP_SENSOR
		| EC_FEATURE_MASK_0(EC_FEATURE_THERMAL)
#endif
#if (defined CONFIG_BACKLIGHT_LID) || (defined CONFIG_BACKLIGHT_REQ_GPIO)
		| EC_FEATURE_MASK_0(EC_FEATURE_BKLIGHT_SWITCH)
#endif
#ifdef CONFIG_WIRELESS
		| EC_FEATURE_MASK_0(EC_FEATURE_WIFI_SWITCH)
#endif
#ifdef CONFIG_HOSTCMD_EVENTS
		| EC_FEATURE_MASK_0(EC_FEATURE_HOST_EVENTS)
#endif
#ifdef CONFIG_COMMON_GPIO
		| EC_FEATURE_MASK_0(EC_FEATURE_GPIO)
#endif
#ifdef CONFIG_I2C_MASTER
		| EC_FEATURE_MASK_0(EC_FEATURE_I2C)
#endif
#ifdef CONFIG_CHARGER
		| EC_FEATURE_MASK_0(EC_FEATURE_CHARGER)
#endif
#if (defined CONFIG_BATTERY)
		| EC_FEATURE_MASK_0(EC_FEATURE_BATTERY)
#endif
#ifdef CONFIG_BATTERY_SMART
		| EC_FEATURE_MASK_0(EC_FEATURE_SMART_BATTERY)
#endif
#ifdef CONFIG_AP_HANG_DETECT
		| EC_FEATURE_MASK_0(EC_FEATURE_HANG_DETECT)
#endif
#if 0
		| EC_FEATURE_MASK_0(EC_FEATURE_PMU) /* Obsolete */
#endif
#ifdef CONFIG_HOSTCMD_PD
		| EC_FEATURE_MASK_0(EC_FEATURE_SUB_MCU)
#endif
#ifdef CONFIG_CHARGE_MANAGER
		| EC_FEATURE_MASK_0(EC_FEATURE_USB_PD)
#endif
#ifdef CONFIG_ACCEL_FIFO
		| EC_FEATURE_MASK_0(EC_FEATURE_MOTION_SENSE_FIFO)
#endif
#ifdef CONFIG_VSTORE
		| EC_FEATURE_MASK_0(EC_FEATURE_VSTORE)
#endif
#ifdef CONFIG_USB_MUX_VIRTUAL
		| EC_FEATURE_MASK_0(EC_FEATURE_USBC_SS_MUX_VIRTUAL)
#endif
#ifdef CONFIG_HOSTCMD_RTC
		| EC_FEATURE_MASK_0(EC_FEATURE_RTC)
#endif
#ifdef CONFIG_SPI_FP_PORT
		| EC_FEATURE_MASK_0(EC_FEATURE_FINGERPRINT)
#endif
#ifdef HAS_TASK_CENTROIDING
		| EC_FEATURE_MASK_0(EC_FEATURE_TOUCHPAD)
#endif
#if defined(HAS_TASK_RWSIG) || defined(HAS_TASK_RWSIG_RO)
		| EC_FEATURE_MASK_0(EC_FEATURE_RWSIG)
#endif
#ifdef CONFIG_DEVICE_EVENT
		| EC_FEATURE_MASK_0(EC_FEATURE_DEVICE_EVENT)
#endif
		;
	return board_override_feature_flags0(result);
}

uint32_t get_feature_flags1(void)
{
	uint32_t result = EC_FEATURE_MASK_1(EC_FEATURE_UNIFIED_WAKE_MASKS)
#ifdef CONFIG_HOST_EVENT64
		| EC_FEATURE_MASK_1(EC_FEATURE_HOST_EVENT64)
#endif
#ifdef CONFIG_EXTERNAL_STORAGE
		| EC_FEATURE_MASK_1(EC_FEATURE_EXEC_IN_RAM)
#endif
#ifdef CONFIG_CEC
		| EC_FEATURE_MASK_1(EC_FEATURE_CEC)
#endif
#ifdef CONFIG_SENSOR_TIGHT_TIMESTAMPS
		| EC_FEATURE_MASK_1(EC_FEATURE_MOTION_SENSE_TIGHT_TIMESTAMPS)
#endif
#if defined(CONFIG_LID_ANGLE) && defined(CONFIG_TABLET_MODE)
		| EC_FEATURE_MASK_1(EC_FEATURE_REFINED_TABLET_MODE_HYSTERESIS)
#endif
#ifdef CONFIG_VBOOT_EFS2
		| EC_FEATURE_MASK_1(EC_FEATURE_EFS2)
#endif
#ifdef CONFIG_IPI
		| EC_FEATURE_MASK_1(EC_FEATURE_SCP)
#endif
#ifdef CHIP_ISH
		| EC_FEATURE_MASK_1(EC_FEATURE_ISH)
#endif
		;
	return board_override_feature_flags1(result);
}

__overridable uint32_t board_override_feature_flags0(uint32_t flags0)
{
	return flags0;
}

__overridable uint32_t board_override_feature_flags1(uint32_t flags1)
{
	return flags1;
}

static int cc_feat(int argc, char **argv)
{
	ccprintf(" 0-31: 0x%08x\n", get_feature_flags0());
	ccprintf("32-63: 0x%08x\n", get_feature_flags1());

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(feat, cc_feat, "", "Print feature flags");
