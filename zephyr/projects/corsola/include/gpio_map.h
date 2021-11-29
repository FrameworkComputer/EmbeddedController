/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

#define GPIO_BATT_PRES_ODL		NAMED_GPIO(ec_batt_pres_odl)

#define GPIO_ENTERING_RW		GPIO_UNIMPLEMENTED

/* daughterboard GPIO remap */
#define GPIO_EN_HDMI_PWR        GPIO_EC_X_GPIO1
#define GPIO_USB_C1_FRS_EN      GPIO_EC_X_GPIO1
#define GPIO_USB_C1_PPC_INT_ODL GPIO_X_EC_GPIO2
#define GPIO_PS185_EC_DP_HPD    GPIO_X_EC_GPIO2
#define GPIO_USB_C1_DP_IN_HPD   GPIO_EC_X_GPIO3
#define GPIO_PS185_PWRDN_ODL    GPIO_EC_X_GPIO3

#ifdef CONFIG_PLATFORM_EC_POWER_BUTTON
	#define PWRBTN_INT()        GPIO_INT(GPIO_POWER_BUTTON_L,              \
					     GPIO_INT_EDGE_BOTH,               \
					     power_button_interrupt)
#else
	#define PWRBTN_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_VOLUME_BUTTONS
	#define VOLBTN_INT(pin)     GPIO_INT(pin,                             \
					     GPIO_INT_EDGE_BOTH,              \
					     button_interrupt)
#else
	#define VOLBTN_INT(pin)
#endif

#ifdef CONFIG_SOC_IT8XXX2
	#define AP_SPI_INT()        GPIO_INT(GPIO_SPI0_CS,                     \
					     GPIO_INT_EDGE_BOTH,               \
					     spi_event)
	#define TCPC_C0_INT()
	#define TCPC_C1_INT()
	#define PPC_C0_INT()
#elif defined(CONFIG_SOC_NPCX9M3F)
	/* The interrupt is configured by dts */
	#define AP_SPI_INT()
	#define TCPC_C0_INT()       GPIO_INT(GPIO_USB_C0_TCPC_INT_ODL,         \
					     GPIO_INT_EDGE_FALLING,            \
					     tcpc_alert_event)
	#define TCPC_C1_INT()       GPIO_INT(GPIO_USB_C1_TCPC_INT_ODL,         \
					     GPIO_INT_EDGE_FALLING,            \
					     tcpc_alert_event)
	#define PPC_C0_INT()        GPIO_INT(GPIO_USB_C0_PPC_INT_ODL,          \
					     GPIO_INT_EDGE_FALLING,            \
					     ppc_interrupt)
#endif

#ifdef CONFIG_PLATFORM_EC_TABLET_MODE
	#define GMR_TABLET_INT()    GPIO_INT(GPIO_TABLET_MODE_L,               \
					     GPIO_INT_EDGE_BOTH,               \
					     gmr_tablet_switch_isr)
#else
	#define GMR_TABLET_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_POWERSEQ_MT8186
	#define WARM_RST_REQ_INT()  GPIO_INT(GPIO_AP_EC_WARM_RST_REQ,          \
					     GPIO_INT_EDGE_RISING,             \
					     chipset_reset_request_interrupt)

	#define AP_IN_SLEEP_INT()   GPIO_INT(GPIO_AP_IN_SLEEP_L,               \
					     GPIO_INT_EDGE_BOTH,               \
					     power_signal_interrupt)

	#define AP_IN_RST_INT()     GPIO_INT(GPIO_AP_EC_SYSRST_ODL,            \
					     GPIO_INT_EDGE_BOTH,               \
					     power_signal_interrupt)

	#define AP_EC_WDTRST_INT()  GPIO_INT(GPIO_AP_EC_WDTRST_L,              \
					     GPIO_INT_EDGE_BOTH,               \
					     power_signal_interrupt)
#else
	#define WARM_RST_REQ_INT()
	#define AP_IN_SLEEP_INT()
	#define AP_IN_RST_INT()
	#define AP_EC_WDTRST_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_ACCEL_LIS2DW12
	#define LID_ACCEL_INT()     GPIO_INT(GPIO_LID_ACCEL_INT_L,             \
					     GPIO_INT_EDGE_FALLING,            \
					     lis2dw12_interrupt)
#else
	#define LID_ACCEL_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_EXTPOWER_GPIO
	#define EXTPWR_INT()        GPIO_INT(GPIO_AC_PRESENT,                  \
					     GPIO_INT_EDGE_BOTH,               \
					     extpower_interrupt)
#else
	#define EXTPWR_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_LID_SWITCH
	#define LID_SWITCH_INT()    GPIO_INT(GPIO_LID_OPEN,                    \
					     GPIO_INT_EDGE_BOTH,               \
					     lid_interrupt)
#else
	#define LID_SWITCH_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_SWITCH
	#define SWITCH_INT()        GPIO_INT(GPIO_WP_L,                        \
					     GPIO_INT_EDGE_BOTH,               \
					     switch_interrupt)
#else
	#define SWITCH_INT()
#endif

#ifdef CONFIG_VARIANT_CORSOLA_DB_DETECTION
	#define X_EC_GPIO2_INT()    GPIO_INT(GPIO_X_EC_GPIO2,                  \
					     GPIO_INT_EDGE_BOTH,               \
					     x_ec_interrupt)
#else
	#define X_EC_GPIO2_INT()
#endif

#ifdef CONFIG_VARIANT_CORSOLA_USBA
	#define USBA_INT() GPIO_INT(GPIO_AP_XHCI_INIT_DONE,                    \
				    GPIO_INT_EDGE_BOTH,                        \
				    usb_a0_interrupt)
#else
	#define USBA_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_GMR_TABLET_MODE
#define GMR_TABLET_MODE_GPIO_L GPIO_TABLET_MODE_L
#endif

#ifdef CONFIG_PLATFORM_EC_ACCELGYRO_ICM42607
	#define BASE_IMU_INT() GPIO_INT(GPIO_BASE_IMU_INT_L,                   \
					GPIO_INT_EDGE_FALLING,                 \
					icm42607_interrupt)
#else
	#define BASE_IMU_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_USB_PD_TCPM_RT1718S
#define GPIO_EN_USB_C1_SINK         RT1718S_GPIO1
#define GPIO_EN_USB_C1_SOURCE       RT1718S_GPIO2
#define GPIO_EN_USB_C1_FRS          RT1718S_GPIO3
#endif

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
#define EC_CROS_GPIO_INTERRUPTS						\
	PWRBTN_INT()							\
	VOLBTN_INT(GPIO_VOLUME_DOWN_L)					\
	VOLBTN_INT(GPIO_VOLUME_UP_L)					\
	LID_SWITCH_INT()						\
	WARM_RST_REQ_INT()						\
	AP_IN_SLEEP_INT()						\
	AP_IN_RST_INT()							\
	AP_EC_WDTRST_INT()						\
	GMR_TABLET_INT()						\
	BASE_IMU_INT()							\
	LID_ACCEL_INT()							\
	USBA_INT()							\
	EXTPWR_INT()							\
	SWITCH_INT()							\
	AP_SPI_INT()							\
	TCPC_C0_INT()							\
	TCPC_C1_INT()							\
	PPC_C0_INT()							\
	X_EC_GPIO2_INT()

#endif /* __ZEPHYR_GPIO_MAP_H */
