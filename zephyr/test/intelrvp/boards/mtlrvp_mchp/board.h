/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MTLRVP_MCHP_BOARD_H_
#define MTLRVP_MCHP_BOARD_H_

#include <dt-bindings/gpio/microchip-xec-gpio.h>

/* Power Signals */
#define PWR_EN_PP3300_S5 MCHP_GPIO_DECODE_025
#define PWR_RSMRST_PWRGD MCHP_GPIO_DECODE_011
#define PWR_EC_PCH_RSMRST MCHP_GPIO_DECODE_054
#define PWR_SLP_S0 MCHP_GPIO_DECODE_002
#define PWR_PCH_PWROK MCHP_GPIO_DECODE_106
#define PWR_EC_PCH_SYS_PWROK MCHP_GPIO_DECODE_202
#define PWR_SYS_RST MCHP_GPIO_DECODE_165
#define PWR_ALL_SYS_PWRGD MCHP_GPIO_DECODE_057
#define STD_ADP_PRSNT MCHP_GPIO_DECODE_043

/* USB-C signals */
#define GPIO_CCD_MODE_ODL MCHP_GPIO_DECODE_242
#define GPIO_USBC_TCPC_ALRT_P0 MCHP_GPIO_DECODE_143
#define GPIO_USB_C0_C1_TCPC_RST_ODL MCHP_GPIO_DECODE_240
#define GPIO_USBC_TCPC_PPC_ALRT_P0 MCHP_GPIO_DECODE_241
#define GPIO_USBC_TCPC_PPC_ALRT_P1 MCHP_GPIO_DECODE_155
#define GPIO_USBC_TCPC_ALRT_P2 MCHP_GPIO_DECODE_221
#define GPIO_USBC_TCPC_ALRT_P3 MCHP_GPIO_DECODE_034

#define I2C_TYPEC_AIC1 i2c_smb_1
#define I2C_TYPEC_AIC2 i2c_smb_2

/* General GPIO Signals */
#define GPIO_KB_DISCRETE_INT MCHP_GPIO_DECODE_175
#define GPIO_PCH_SLP_S3_L MCHP_GPIO_DECODE_014 /* For pm-slp-s3-n-ec */
#define PM_SLP_S4_N_EC MCHP_GPIO_DECODE_015 /* For pm-slp-s4-n-ec */
#define GPIO_VOLUME_UP_L MCHP_GPIO_DECODE_036 /* For volume-up */
#define GPIO_VOLUME_DOWN_L MCHP_GPIO_DECODE_254 /* For vol-dn-ec-r */
#define GPIO_LID_OPEN MCHP_GPIO_DECODE_266 /* For smc_lid */
#define GPIO_POWER_BUTTON_L MCHP_GPIO_DECODE_246 /* For smc_onoff_n */
#define GPIO_AC_PRESENT MCHP_GPIO_DECODE_156 /* For bc_acok */
#define PCH_WAKE_N MCHP_GPIO_DECODE_051 /* For gpio_ec_pch_wake_odl */
#define PLT_RST_L MCHP_GPIO_DECODE_052 /* For plt-rst-l */
#define SLATE_MODE_INDICATION \
	MCHP_GPIO_DECODE_222 /* For slate-mode-indication */
#define GPIO_CPU_PROCHOT MCHP_GPIO_DECODE_243 /* For prochot-ec */
#define GPIO_PCH_PWRBTN_L MCHP_GPIO_DECODE_245 /* For pm-pwrbtn-n-ec */
#define EC_SPI_OE_MECC MCHP_GPIO_DECODE_053 /* For ec_spi_oe_mecc */
#define GPIO_BATT_PRES_ODL MCHP_GPIO_DECODE_206 /* For bat-det-ec */
#define EDP_BKLT_EN MCHP_GPIO_DECODE_022 /* For edp-bklt-en */
#define LED_1_L_EC MCHP_GPIO_DECODE_157 /* For led-1-l-ec */
#define LED_2_L_EC MCHP_GPIO_DECODE_153 /* For led-2-l-ec */
#define GPIO_FAN_CONTROL MCHP_GPIO_DECODE_201 /* For gpio_fan_control */
#define GPIO_WP MCHP_GPIO_DECODE_024

#define KEYBOARD_COMPAT "cros-keyscan"
#define KEYBOARD_OUTPUT_SETTLE 80
#define KEYBOARD_DEBOUNCE_DOWN 9000
#define KEYBOARD_DEBOUNCE_UP 40000

#endif /* MTLRVP_MCHP_BOARD_H_ */