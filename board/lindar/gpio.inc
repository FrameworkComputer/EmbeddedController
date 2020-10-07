/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Wake Source interrupts */
GPIO_INT(EC_LID_OPEN,          PIN(D, 2), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, lid_interrupt)
GPIO_INT(EC_WP_L,              PIN(A, 1), GPIO_INT_BOTH, switch_interrupt)
GPIO_INT(H1_EC_PWR_BTN_ODL,    PIN(0, 1), GPIO_INT_BOTH, power_button_interrupt)
GPIO_INT(ACOK_OD,              PIN(0, 0), GPIO_INT_BOTH | GPIO_HIB_WAKE_HIGH, extpower_interrupt)

/* Power sequencing interrupts */
GPIO_INT(SLP_S0_L,             PIN(D, 5), GPIO_INT_BOTH, power_signal_interrupt)
#ifndef CONFIG_HOSTCMD_ESPI_VW_SLP_S3
GPIO_INT(SLP_S3_L,             PIN(A, 5), GPIO_INT_BOTH, power_signal_interrupt)
#endif
GPIO_INT(SLP_SUS_L,            PIN(D, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_RSMRST_ODL,     PIN(E, 2), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_DSW_PWROK,      PIN(C, 7), GPIO_INT_BOTH, power_signal_interrupt)
GPIO_INT(PG_EC_ALL_SYS_PWRGD,  PIN(F, 4),  GPIO_INT_BOTH, power_signal_interrupt)

/* Sensor Interrupts */
GPIO_INT(EC_IMU_INT_L,		PIN(5, 6), GPIO_INT_FALLING | GPIO_SEL_1P8V, lsm6dsm_interrupt)
GPIO(EC_ACCEL_INT_L,		PIN(8, 1), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO_INT(TABLET_MODE_L,		PIN(9, 5), GPIO_INT_BOTH, gmr_tablet_switch_isr)

/* USB-C interrupts */
GPIO_INT(USB_C0_TCPC_INT_ODL,	PIN(E, 0), GPIO_INT_BOTH, tcpc_alert_event)
GPIO_INT(USB_C1_TCPC_INT_ODL,	PIN(A, 2), GPIO_INT_BOTH, tcpc_alert_event)

GPIO_INT(USB_C0_PPC_INT_ODL,	PIN(6, 2), GPIO_INT_BOTH, ppc_interrupt)
GPIO_INT(USB_C1_PPC_INT_ODL,	PIN(F, 5), GPIO_INT_BOTH, ppc_interrupt)

GPIO_INT(USB_C0_BC12_INT_ODL,	PIN(E, 4), GPIO_INT_BOTH, bc12_interrupt)
GPIO_INT(USB_C1_MIX_INT_ODL,	PIN(0, 3), GPIO_INT_BOTH, bc12_interrupt)

/* HDMI interrupts */

/* Volume button interrupts */
GPIO_INT(EC_VOLDN_MUTE_BTN_ODL,		PIN(9, 3), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_VOLUP_BTN_ODL,		PIN(9, 7), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)

/* HW_ID PIN */
GPIO(HW_ID,		PIN(D, 4), GPIO_INPUT)

/* Power Sequencing Signals */
GPIO(EN_PP3300_A,              PIN(A, 3), GPIO_OUT_LOW)
GPIO(EN_PP5000_A,              PIN(A, 4), GPIO_OUT_LOW)
GPIO(EN_PPVAR_VCCIN,           PIN(4, 3), GPIO_OUT_LOW) /* Enables VCCIN IMPV9 */


/* The EC does not buffer this signal on Volteer. */
UNIMPLEMENTED(PCH_DSW_PWROK)

/* Other wake sources */
/*
 * GPIO_INT_BOTH is required for PSL wake from hibernate, but we don't need an
 * interrupt handler because it is automatically handled by the PSL.
 *
 * We need to lock the setting so this gpio can't be reconfigured to overdrive
 * the real reset signal. (This is the PSL input pin not the real reset pin).
 */
GPIO(EC_RST_ODL,		PIN(0, 2), GPIO_INT_BOTH |
					   GPIO_HIB_WAKE_HIGH |
					   GPIO_LOCKED)

/* AP/PCH Signals */
GPIO(EC_PCH_SYS_PWROK,         PIN(3, 7), GPIO_OUT_LOW)
GPIO(EC_PCH_RSMRST_ODL,        PIN(A, 6), GPIO_ODR_LOW) /* TODO - b/140950085 - implement TGL sequencing requirement */
GPIO(EC_PCH_PWR_BTN_ODL,       PIN(C, 1), GPIO_ODR_HIGH)
GPIO(EC_PCH_RTCRST,            PIN(7, 6), GPIO_OUT_LOW)
GPIO(EC_PCH_WAKE_ODL,          PIN(7, 4), GPIO_ODR_HIGH)
GPIO(EC_ENTERING_RW,           PIN(E, 3), GPIO_OUT_LOW)
GPIO(EC_PROCHOT_ODL,           PIN(6, 3), GPIO_ODR_HIGH)
UNIMPLEMENTED(EC_PROCHOT_IN_L)
GPIO(SYS_RST_ODL,              PIN(C, 5), GPIO_ODR_HIGH)

GPIO(EC_PCH_INT_ODL,           PIN(B, 0), GPIO_ODR_HIGH)

/* USB and USBC Signals */
GPIO(USB_C1_RT_RST_ODL,			PIN(8, 3), GPIO_ODR_LOW) /* USB_C1 Reset on boards board ID >=1 */
GPIO(USB_C0_OC_ODL,			PIN(B, 1), GPIO_ODR_HIGH)
GPIO(USB_C1_OC_ODL,			PIN(5, 0), GPIO_ODR_HIGH)

/* Don't have a load switch for retimer */
UNIMPLEMENTED(USB_C1_LS_EN)

/* Misc Signals */
GPIO(EC_H1_PACKET_MODE,		PIN(7, 5), GPIO_OUT_LOW)	/* H1 Packet Mode */

/*
 * Determine the polarity of these SSD signals and whether
 * they are open-drain.
 */
GPIO(M2_SSD_PLN,		PIN(A, 0), GPIO_OUT_HIGH)	/* SSD power-loss notification */
GPIO(M2_SSD_PLA,		PIN(7, 0), GPIO_INPUT)		/* SSD power-loss acknoledgement */

/*
 * eDP backlight - both PCH and EC have enable pins that must be high
 * for the backlight to turn on. Default state is high, and can be turned
 * off during sleep states.
 */
GPIO(EC_EDP_BL_EN,             PIN(D, 3), GPIO_OUT_HIGH)

/* I2C pins - Alternate function below configures I2C module on these pins */
GPIO(EC_I2C0_SENSOR_SCL,       PIN(B, 5), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C0_SENSOR_SDA,       PIN(B, 4), GPIO_INPUT | GPIO_SEL_1P8V)
GPIO(EC_I2C1_USB_C0_SCL,       PIN(9, 0), GPIO_INPUT)
GPIO(EC_I2C1_USB_C0_SDA,       PIN(8, 7), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SCL,       PIN(9, 2), GPIO_INPUT)
GPIO(EC_I2C2_USB_C1_SDA,       PIN(9, 1), GPIO_INPUT)
GPIO(EC_I2C3_LEDBAR_SCL,     PIN(D, 1), GPIO_INPUT)
GPIO(EC_I2C3_LEDBAR_SDA,     PIN(D, 0), GPIO_INPUT)
GPIO(EC_I2C5_POWER_SCL,        PIN(3, 3), GPIO_INPUT)
GPIO(EC_I2C5_POWER_SDA,        PIN(3, 6), GPIO_INPUT)
GPIO(EC_I2C7_EEPROM_SCL,       PIN(B, 3), GPIO_INPUT)
GPIO(EC_I2C7_EEPROM_SDA,       PIN(B, 2), GPIO_INPUT)

/* Battery signals */
GPIO(EC_BATT_PRES_ODL, 		PIN(E, 1), GPIO_INPUT)

/* LED */
GPIO(LED_1_L,	 PIN(C, 4), GPIO_OUT_HIGH)	/* Battery - Green LED */
GPIO(LED_2_L,	 PIN(C, 3), GPIO_OUT_HIGH)	/* Battery - Red LED */
GPIO(LED_3_L,	 PIN(C, 2), GPIO_OUT_HIGH)	/* Power - White LED */

/* Alternate functions GPIO definitions */
ALTERNATE(PIN_MASK(B, BIT(5) | BIT(4)), 0, MODULE_I2C, (GPIO_INPUT | GPIO_SEL_1P8V)) /* I2C0 */
ALTERNATE(PIN_MASK(9, BIT(0) | BIT(2) | BIT(1)), 0, MODULE_I2C, 0)                   /* I2C1 SCL / I2C2 */
ALTERNATE(PIN_MASK(8, BIT(7)), 0, MODULE_I2C, 0)                                     /* I2C1 SDA */
ALTERNATE(PIN_MASK(D, BIT(1) | BIT(0)), 0, MODULE_I2C, 0)                            /* I2C3 */
ALTERNATE(PIN_MASK(3, BIT(3) | BIT(6)), 0, MODULE_I2C, 0)                            /* I2C5 */
ALTERNATE(PIN_MASK(B, BIT(3) | BIT(2)), 0, MODULE_I2C, 0)                            /* I2C7 */

/* Fan signals */
GPIO(EN_PP5000_FAN,            PIN(6, 1), GPIO_OUT_LOW)
ALTERNATE(PIN_MASK(B, BIT(7)), 0, MODULE_PWM, 0)	/* FAN_PWM */
ALTERNATE(PIN_MASK(4, BIT(0)), 0, MODULE_PWM, 0)	/* FAN_SPEED_TACH */

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
ALTERNATE(PIN_MASK(3, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* KSI_00-01 */
ALTERNATE(PIN_MASK(2, 0xFC), 0, MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* KSI_02-07 */
ALTERNATE(PIN_MASK(2, 0x03), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_00-01 */
GPIO(EC_KSO_02_INV, PIN(1, 7),                        GPIO_OUT_LOW)  /* KSO_02 */
ALTERNATE(PIN_MASK(1, 0x7F), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_03-09 */
ALTERNATE(PIN_MASK(0, 0xF0), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_10-13 */
ALTERNATE(PIN_MASK(8, 0x04), 0, MODULE_KEYBOARD_SCAN, GPIO_ODR_HIGH) /* KSO_14 */
ALTERNATE(PIN_MASK(8, BIT(0)), 0, MODULE_PWM, 0)                     /* EC_KB_BL_PWM */

/* UART */
ALTERNATE(PIN_MASK(6, BIT(5) | BIT(4)), 0, MODULE_UART, 0)                           /* UART from EC to Servo */

/* Power Switch Logic (PSL) inputs */
ALTERNATE(PIN_MASK(D, BIT(2)), 0, MODULE_PMU, 0)                    /* GPIOD2 = EC_LID_OPEN */
ALTERNATE(PIN_MASK(0, BIT(0) | BIT(1) | BIT(2)), 0, MODULE_PMU, 0)  /* GPIO00 = ACOK_OD,
                                                                       GPIO01 = H1_EC_PWR_BTN_ODL
                                                                       GPIO02 = EC_RST_ODL */

/* Unused signals */
GPIO(UNUSED_GPIO60,    PIN(6, 0), GPIO_INPUT | GPIO_PULL_UP)
GPIO(UNUSED_GPIO34,    PIN(3, 4), GPIO_INPUT | GPIO_PULL_UP)
GPIO(UNUSED_GPIO96,    PIN(9, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(UNUSED_GPIOF2,    PIN(F, 2), GPIO_INPUT | GPIO_PULL_UP)
GPIO(UNUSED_GPIOF3,    PIN(F, 3), GPIO_INPUT | GPIO_PULL_UP)
