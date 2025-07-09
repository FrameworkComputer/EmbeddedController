/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MTLRVP_NPCX_BOARD_H_
#define MTLRVP_NPCX_BOARD_H_

/* Power Signals */
#define PWR_EN_PP3300_S5 &gpioc 4
#define PWR_RSMRST_PWRGD &gpio6 6
#define PWR_EC_PCH_RSMRST &gpioa 4
#define PWR_SLP_S0 &gpioa 1
#define PWR_PCH_PWROK &gpiod 3
#define PWR_EC_PCH_SYS_PWROK &gpiof 5
#define PWR_SYS_RST &gpioc 5
#define PWR_ALL_SYS_PWRGD &gpio7 0
#define STD_ADP_PRSNT &gpioc 6

/* USB-C signals */
#define GPIO_CCD_MODE_ODL &gpio9 2
#define GPIO_USBC_TCPC_ALRT_P0 &gpio4 0
#define GPIO_USB_C0_C1_TCPC_RST_ODL &gpiod 0
#define GPIO_USBC_TCPC_PPC_ALRT_P0 &gpiod 1
#define GPIO_USBC_TCPC_PPC_ALRT_P1 &gpioe 4
#define GPIO_USBC_TCPC_ALRT_P2 &gpio9 1
#define GPIO_USBC_TCPC_ALRT_P3 &gpiof 3

#define I2C_TYPEC_AIC1 i2c0_0
#define I2C_TYPEC_AIC2 i2c1_0

/* General GPIO Signals */
#define GPIO_KB_DISCRETE_INT &gpio0 0 /* For ioex_kbd_intr_n */
#define GPIO_PCH_SLP_S3_L &gpiob 0 /* For pm-slp-s3-n-ec */
#define PM_SLP_S4_N_EC &gpioa 5 /* For pm-slp-s4-n-ec */
#define GPIO_VOLUME_UP_L &gpio6 1 /* For volume-up */
#define GPIO_VOLUME_DOWN_L &gpio0 3 /* For vol-dn-ec-r */
#define GPIO_LID_OPEN &gpio0 1 /* For smc_lid */
#define GPIO_POWER_BUTTON_L &gpiod 2 /* For smc_onoff_n */
#define GPIO_AC_PRESENT &gpio0 2 /* For bc_acok */
#define PCH_WAKE_N &gpio7 4 /* For gpio_ec_pch_wake_odl */
#define ESPI_RST_N &gpio5 4 /* For espi-rst-n */
#define PLT_RST_L &gpioa 2 /* For plt-rst-l */
#define SLATE_MODE_INDICATION &gpio9 4 /* For slate-mode-indication */
#define GPIO_CPU_PROCHOT &gpio6 0 /* For prochot-ec */
#define GPIO_PCH_PWRBTN_L &gpiod 4 /* For pm-pwrbtn-n-ec */
#define EC_SPI_OE_MECC &gpioa 7 /* For ec_spi_oe_mecc */
#define GPIO_BATT_PRES_ODL &gpio7 6 /* For bat-det-ec */
#define EDP_BKLT_EN &gpioe 1 /* For edp-bklt-en */
#define LED_1_L_EC &gpiob 6 /* For led-1-l-ec */
#define LED_2_L_EC &gpiob 7 /* For led-2-l-ec */
#define GPIO_FAN_CONTROL &gpioc 0 /* For gpio_fan_control */
#define SMB_BS_CLK &gpiob 3 /* For smb-bs-clk */
#define SMB_BS_DATA &gpiob 2 /* For smb-bs-data */
#define USBC_TCPC_I2C_CLK_AIC1 &gpiob 5 /* For usbc-tcpc-i2c-clk-aic1 */
#define USBC_TCPC_I2C_DATA_AIC1 &gpiob 4 /* For usbc-tcpc-i2c-data-aic1 */
#define USBC_TCPC_I2C_CLK_AIC2 &gpio9 0 /* For usbc-tcpc-i2c-clk-aic2 */
#define USBC_TCPC_I2C_DATA_AIC2 &gpio8 7 /* For usbc-tcpc-i2c-data-aic2 */
#define I3C_1_SDA_R &gpio5 0 /* For i3c-1-sda-r */
#define I3C_1_SCL_R &gpio5 6 /* For i3c-1-scl-r */
#define GPIO_WP &gpiod 5

#define KEYBOARD_COMPAT "cros-ec,keyscan"
#define KEYBOARD_OUTPUT_SETTLE 35
#define KEYBOARD_DEBOUNCE_DOWN 5000
#define KEYBOARD_DEBOUNCE_UP 30000

#endif /* MTLRVP_NPCX_BOARD_H_ */