/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

#define GPIO_ENTERING_RW	GPIO_UNIMPLEMENTED
#define GPIO_WP_L		GPIO_UNIMPLEMENTED

#ifdef CONFIG_PLATFORM_EC_ALS_TCS3400
#define TCS3400_INT(gpio, edge) GPIO_INT(gpio, edge, tcs3400_interrupt)
#else
#define TCS3400_INT(gpio, edge)
#endif

#ifdef CONFIG_PLATFORM_EC_ACCELGYRO_LSM6DSO
#define LSM6DSO_INT(gpio, edge) GPIO_INT(gpio, edge, lsm6dso_interrupt)
#else
#define LSM6DSO_INT(gpio, edge)
#endif

#ifdef CONFIG_PLATFORM_EC_ACCEL_LIS2DW12
#define LIS2DW12_INT(gpio, edge) GPIO_INT(gpio, edge, lis2dw12_interrupt)
#else
#define LIS2DW12_INT(gpio, edge)
#endif

#ifdef CONFIG_PLATFORM_EC_GMR_TABLET_MODE
#define GMR_TABLET_MODE_INT(gpio, edge) GPIO_INT(gpio, edge, \
						 gmr_tablet_switch_isr)
#define GMR_TABLET_MODE_GPIO_L	GPIO_TABLET_MODE_L
#else
#define GMR_TABLET_MODE_INT(gpio, edge)
#endif

#ifdef CONFIG_PLATFORM_EC_USBC
#define TCPC_ALERT_INT(gpio, edge) GPIO_INT(gpio, edge, tcpc_alert_event)
#define PPC_INT(gpio, edge) GPIO_INT(gpio, edge, ppc_interrupt)
#define BC12_INT(gpio, edge) GPIO_INT(gpio, edge, bc12_interrupt)
#define RETIMER_INT(gpio, edge) GPIO_INT(gpio, edge, retimer_interrupt)
#else
#define TCPC_ALERT_INT(gpio, edge)
#define PPC_INT(gpio, edge)
#define BC12_INT(gpio, edge)
#define RETIMER_INT(gpio, edge)
#endif /* CONFIG_PLATFORM_EC_USBC */

/*
 * Set EC_CROS_GPIO_INTERRUPTS to a space-separated list of GPIO_INT items.
 *
 * Each GPIO_INT requires three parameters:
 *   gpio_signal - The enum gpio_signal for the interrupt gpio
 *   interrupt_flags - The interrupt-related flags (e.g. GPIO_INT_EDGE_BOTH)
 *   handler - The platform/ec interrupt handler.
 *
 * Ensure that this files includes all necessary headers to declare all
 * referenced handler functions.
 *
 * For example, one could use the follow definition:
 * #define EC_CROS_GPIO_INTERRUPTS \
 *   GPIO_INT(NAMED_GPIO(h1_ec_pwr_btn_odl), GPIO_INT_EDGE_BOTH, button_print)
 */
/* Helper macros for generating CROS_EC_GPIO_INTERRUPTS */
#ifdef CONFIG_PLATFORM_EC_POWERSEQ
#define POWER_SIGNAL_INT(gpio, edge) \
	GPIO_INT(gpio, edge, power_signal_interrupt)
#define AP_PROCHOT_INT(gpio, edge) \
	GPIO_INT(gpio, edge, throttle_ap_prochot_input_interrupt)
#else
#define POWER_SIGNAL_INT(gpio, edge)
#define AP_PROCHOT_INT(gpio, edge)
#endif

#define GPIO_EC_BATT_PRES_ODL GPIO_BATT_PRES_ODL
#define GPIO_ID_1_EC_KB_BL_EN	GPIO_EC_BATT_PRES_ODL
#define GPIO_SEQ_EC_DSW_PWROK GPIO_PG_EC_DSW_PWROK

#define EC_CROS_GPIO_INTERRUPTS                                           \
	GMR_TABLET_MODE_INT(GPIO_TABLET_MODE_L, GPIO_INT_EDGE_BOTH)       \
	GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)        \
	GPIO_INT(GPIO_POWER_BUTTON_L, GPIO_INT_EDGE_BOTH,                 \
		 power_button_interrupt)                                  \
	GPIO_INT(GPIO_WP_L, GPIO_INT_EDGE_BOTH, switch_interrupt)         \
	GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt) \
	GPIO_INT(GPIO_VOLUME_DOWN_L, GPIO_INT_EDGE_BOTH, button_interrupt)\
	GPIO_INT(GPIO_VOLUME_UP_L, GPIO_INT_EDGE_BOTH, button_interrupt)  \
	LIS2DW12_INT(GPIO_EC_ACCEL_INT, GPIO_INT_EDGE_FALLING)           \
	LSM6DSO_INT(GPIO_EC_IMU_INT_L, GPIO_INT_EDGE_FALLING)             \
	POWER_SIGNAL_INT(GPIO_PCH_SLP_S0_L, GPIO_INT_EDGE_BOTH)           \
	POWER_SIGNAL_INT(GPIO_PCH_SLP_S3_L, GPIO_INT_EDGE_BOTH)           \
	POWER_SIGNAL_INT(GPIO_SLP_SUS_L, GPIO_INT_EDGE_BOTH)              \
	POWER_SIGNAL_INT(GPIO_PG_EC_DSW_PWROK, GPIO_INT_EDGE_BOTH)        \
	POWER_SIGNAL_INT(GPIO_PG_EC_RSMRST_ODL, GPIO_INT_EDGE_BOTH)       \
	POWER_SIGNAL_INT(GPIO_PG_EC_ALL_SYS_PWRGD, GPIO_INT_EDGE_BOTH)    \
	TCS3400_INT(GPIO_EC_ALS_RGB_INT_L, GPIO_INT_EDGE_FALLING)         \
	AP_PROCHOT_INT(GPIO_EC_PROCHOT_IN_L, GPIO_INT_EDGE_BOTH)          \
	TCPC_ALERT_INT(GPIO_USB_C0_C2_TCPC_INT_ODL, GPIO_INT_EDGE_FALLING)\
	TCPC_ALERT_INT(GPIO_USB_C1_TCPC_INT_ODL, GPIO_INT_EDGE_FALLING)   \
	PPC_INT(GPIO_USB_C0_PPC_INT_ODL, GPIO_INT_EDGE_FALLING)           \
	PPC_INT(GPIO_USB_C1_PPC_INT_ODL, GPIO_INT_EDGE_FALLING)           \
	PPC_INT(GPIO_USB_C2_PPC_INT_ODL, GPIO_INT_EDGE_FALLING)           \
	BC12_INT(GPIO_USB_C0_BC12_INT_ODL, GPIO_INT_EDGE_FALLING)         \
	BC12_INT(GPIO_USB_C1_BC12_INT_ODL, GPIO_INT_EDGE_FALLING)         \
	BC12_INT(GPIO_USB_C2_BC12_INT_ODL, GPIO_INT_EDGE_FALLING)         \
	RETIMER_INT(GPIO_USB_C0_RT_INT_ODL, GPIO_INT_EDGE_FALLING)        \
	RETIMER_INT(GPIO_USB_C2_RT_INT_ODL, GPIO_INT_EDGE_FALLING)
#endif /* __ZEPHYR_GPIO_MAP_H */
