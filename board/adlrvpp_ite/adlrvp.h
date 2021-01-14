/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-RVP specific configuration */

#ifndef __ADLRVP_BOARD_H
#define __ADLRVP_BOARD_H

/* Temperature sensor */
#define CONFIG_TEMP_SENSOR

#include "baseboard.h"

/* MECC config */
#define CONFIG_INTEL_RVP_MECC_VERSION_1_0

/* Support early firmware selection */
#define CONFIG_VBOOT_EFS2

/* Chipset */
#define CONFIG_CHIPSET_ALDERLAKE

/* USB PD config */
#if defined(HAS_TASK_PD_C2) && defined(HAS_TASK_PD_C3)
#define CONFIG_USB_PD_PORT_MAX_COUNT 4
#else
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#endif
#define CONFIG_USB_MUX_VIRTUAL
#define PD_MAX_POWER_MW              100000

/* TCPC AIC config */
/* Support NXP PCA9675 I/O expander. */
#define CONFIG_IO_EXPANDER_PCA9675
#define I2C_ADDR_PCA9675_TCPC_AIC_IOEX	0x21
#define CONFIG_IO_EXPANDER_PORT_COUNT CONFIG_USB_PD_PORT_MAX_COUNT

/* DC Jack charge ports */
#undef  CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
#define DEDICATED_CHARGE_PORT CONFIG_USB_PD_PORT_MAX_COUNT

/* PPC */
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_DISCHARGE_PPC
#define I2C_ADDR_SN5S330_TCPC_AIC_PPC	0x40

/* TCPC */
#define CONFIG_USB_PD_DISCHARGE
#define CONFIG_USB_PD_TCPM_FUSB302
#define I2C_ADDR_FUSB302_TCPC_AIC	0x22

/* Config BB retimer */
#define CONFIG_USBC_RETIMER_INTEL_BB
#define I2C_PORT0_BB_RETIMER_ADDR	0x56
#define I2C_PORT1_BB_RETIMER_ADDR	0x57
#if defined(HAS_TASK_PD_C2)
#define I2C_PORT2_BB_RETIMER_ADDR	0x58
#endif
#if defined(HAS_TASK_PD_C3)
#define I2C_PORT3_BB_RETIMER_ADDR	0x59
#endif

/* Enable VCONN */
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#define CONFIG_USB_PD_USB4

/* Config Fan */
#define CONFIG_FANS		1
#define BOARD_FAN_MIN_RPM	3000
#define BOARD_FAN_MAX_RPM	10000

/*
 * TCPC AIC used on all the ports are identical expect the I2C lines which
 * are on the respective TCPC port's EC I2C line. Hence, I2C address and
 * the GPIOs to control the retimers are also same for all the ports.
 */
#define TCPC_AIC_IOE_BB_RETIMER_RST	PCA9675_IO_P00
#define TCPC_AIC_IOE_BB_RETIMER_LS_EN	PCA9675_IO_P01
#define TCPC_AIC_IOE_USB_MUX_CNTRL_1	PCA9675_IO_P04
#define TCPC_AIC_IOE_USB_MUX_CNTRL_0	PCA9675_IO_P05
#define TCPC_AIC_IOE_OC			PCA9675_IO_P10

#define TCPC_AIC_IOE_DIRECTION (PCA9675_DEFAULT_IO_DIRECTION & \
	~(TCPC_AIC_IOE_BB_RETIMER_RST | TCPC_AIC_IOE_BB_RETIMER_LS_EN | \
	TCPC_AIC_IOE_USB_MUX_CNTRL_1 | TCPC_AIC_IOE_USB_MUX_CNTRL_0 | \
	TCPC_AIC_IOE_OC))

/* Charger */
#define CONFIG_CHARGER_ISL9241

/* Port 80 */
#define PORT80_I2C_ADDR		MAX695X_I2C_ADDR1_FLAGS

/* Board Id */
#define I2C_ADDR_PCA9555_BOARD_ID_GPIO	0x22

#ifndef __ASSEMBLER__

enum adlrvp_charge_ports {
	TYPE_C_PORT_0,
	TYPE_C_PORT_1,
#if defined(HAS_TASK_PD_C2)
	TYPE_C_PORT_2,
#endif
#if defined(HAS_TASK_PD_C3)
	TYPE_C_PORT_3,
#endif
};

enum battery_type {
	BATTERY_GETAC_SMP_HHP_408,
	BATTERY_TYPE_COUNT,
};

void espi_reset_pin_asserted_interrupt(enum gpio_signal signal);
void extpower_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);
void board_connect_c0_sbu(enum gpio_signal s);
int board_get_version(void);

#endif /* !__ASSEMBLER__ */

#endif /* __ADLRVP_BOARD_H */
