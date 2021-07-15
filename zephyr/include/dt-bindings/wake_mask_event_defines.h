/*
 * Copyright 2021 Google LLC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef DT_BINDINGS_WAKE_MASK_EVENT_DEFINES_H_
#define DT_BINDINGS_WAKE_MASK_EVENT_DEFINES_H_

#ifndef BIT
#define BIT(n) (1U << n)
#endif

/*
 * NOTE: The convention in the Zephyr code is to have the public header file
 * include the dt-binding header file to avoid duplicate definitions.
 * However, ec_commands.h is shared with the linux kernel so we can't do that.
 *
 * Please consult include/ec_commands.h for explanations of the macros
 * defined in this file.
 */

#define MKBP_EVENT_KEY_MATRIX		BIT(0)
#define MKBP_EVENT_HOST_EVENT		BIT(1)
#define MKBP_EVENT_SENSOR_FIFO		BIT(2)
#define MKBP_EVENT_BUTTON		BIT(3)
#define MKBP_EVENT_SWITCH		BIT(4)
#define MKBP_EVENT_FINGERPRINT		BIT(5)
#define MKBP_EVENT_SYSRQ		BIT(6)
#define MKBP_EVENT_HOST_EVENT64		BIT(7)
#define MKBP_EVENT_CEC_EVENT		BIT(8)
#define MKBP_EVENT_CEC_MESSAGE		BIT(9)
#define MKBP_EVENT_DP_ALT_MODE_ENTERED	BIT(10)
#define MKBP_EVENT_ONLINE_CALIBRATION	BIT(11)
#define MKBP_EVENT_PCHG			BIT(12)

#define HOST_EVENT_MASK(event)		((event) >> 1)

#define HOST_EVENT_NONE			BIT(0)
#define HOST_EVENT_LID_CLOSED		BIT(1)
#define HOST_EVENT_LID_OPEN		BIT(2)
#define HOST_EVENT_POWER_BUTTON		BIT(3)
#define HOST_EVENT_AC_CONNECTED		BIT(4)
#define HOST_EVENT_AC_DISCONNECTED	BIT(5)
#define HOST_EVENT_BATTERY_LOW		BIT(6)
#define HOST_EVENT_BATTERY_CRITICAL	BIT(7)
#define HOST_EVENT_BATTERY		BIT(8)
#define HOST_EVENT_THERMAL_THRESHOLD	BIT(9)
#define HOST_EVENT_DEVICE		BIT(10)
#define HOST_EVENT_THERMAL		BIT(11)
#define HOST_EVENT_USB_CHARGER		BIT(12)
#define HOST_EVENT_KEY_PRESSED		BIT(13)
#define HOST_EVENT_INTERFACE_READY	BIT(14)
#define HOST_EVENT_KEYBOARD_RECOVERY	BIT(15)
#define HOST_EVENT_THERMAL_SHUTDOWN	BIT(16)
#define HOST_EVENT_BATTERY_SHUTDOWN	BIT(17)
#define HOST_EVENT_THROTTLE_START	BIT(18)
#define HOST_EVENT_THROTTLE_STOP	BIT(19)
#define HOST_EVENT_HANG_DETECT		BIT(20)
#define HOST_EVENT_HANG_REBOOT		BIT(21)
#define HOST_EVENT_PD_MCU		BIT(22)
#define HOST_EVENT_BATTERY_STATUS	BIT(23)
#define HOST_EVENT_PANIC		BIT(24)
#define HOST_EVENT_KEYBOARD_FASTBOOT	BIT(25)
#define HOST_EVENT_RTC			BIT(26)
#define HOST_EVENT_MKBP			BIT(27)
#define HOST_EVENT_USB_MUX		BIT(28)
#define HOST_EVENT_MODE_CHANGE		BIT(29)
#define HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT	BIT(30)
#define HOST_EVENT_WOV			BIT(31)
#define HOST_EVENT_INVALID		BIT(32)

#endif /* DT_BINDINGS_WAKE_MASK_EVENT_DEFINES_H_ */

