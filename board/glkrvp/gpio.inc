/* -*- mode:c -*-
 *
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel GLK-RVP board-specific configuration */

/*
 * Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first.
 */
GPIO_INT(PCH_SLP_S4_L,		PIN(8, 6), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S4_L */
GPIO_INT(PCH_SLP_S3_L,		PIN(8, 5), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S3_L */
GPIO_INT(PCH_SLP_S0_L,		PIN(8, 1), GPIO_INT_BOTH, power_signal_interrupt)	/* SLP_S0_L */
GPIO_INT(RSMRST_L_PGOOD,	PIN(3, 6), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_RSMRST_ODL */
GPIO_INT(ALL_SYS_PGOOD,		PIN(7, 2), GPIO_INT_BOTH, power_signal_interrupt)	/* PMIC_EC_PWROK_ODL */
GPIO_INT(POWER_BUTTON_L,	PIN(A, 6), GPIO_INT_BOTH, power_button_interrupt)	/* MECH_PWR_BTN_ODL */
GPIO_INT(LID_OPEN,		PIN(0, 3), GPIO_INT_BOTH | GPIO_PULL_UP, lid_interrupt)	/* SMC_LID */
GPIO_INT(AC_PRESENT,		PIN(D, 2), GPIO_INT_BOTH, extpower_interrupt)		/* ACOK_OD from ISL9238 */
GPIO_INT(WP_L,			PIN(9, 3), GPIO_INT_BOTH | GPIO_SEL_1P8V, switch_interrupt)	/* EC_WP_ODL */
GPIO_INT(USB_C0_PD_INT_ODL,	PIN(6, 2), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(USB_C1_PD_INT_ODL,	PIN(6, 3), GPIO_INT_FALLING, tcpc_alert_event)
GPIO_INT(EC_VOLUP_BTN_ODL,	PIN(3, 4), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)
GPIO_INT(EC_VOLDN_BTN_ODL,	PIN(3, 7), GPIO_INT_BOTH | GPIO_PULL_UP, button_interrupt)

GPIO(PCH_PWRBTN_L,	PIN(7, 5), GPIO_ODR_HIGH)		/* EC_PCH_PWR_BTN_ODL */
GPIO(PCH_WAKE_L,	PIN(C, 1), GPIO_ODR_HIGH)		/* EC_PCH_WAKE_ODL */
GPIO(PCH_SYS_PWROK,	PIN(3, 5), GPIO_OUT_LOW)		/* EC_PCH_PWROK */
GPIO(ENABLE_BACKLIGHT,	PIN(9, 7), GPIO_ODR_HIGH)		/* EC_BL_EN_OD */
GPIO(ENTERING_RW,	PIN(A, 7), GPIO_OUTPUT)			/* EC_ENTERING_RW */

GPIO(PCH_RSMRST_L,	PIN(0, 1), GPIO_OUT_LOW)		/* EC_PCH_RSMRST_L */
GPIO(PCH_RCIN_L,	PIN(0, 0), GPIO_ODR_HIGH)		/* SYS_RST_ODL */
GPIO(SMC_SHUTDOWN,	PIN(3, 3), GPIO_OUT_LOW | GPIO_PULL_DOWN)	/* A_RAIL_EN */

/*
 * PCH_PROCHOT_ODL is primarily for monitoring the PROCHOT# signal which is
 * normally driven by the PMIC. The EC can also drive this signal in the event
 * that the ambient or charger temperature sensors exceeds their thresholds.
 */
GPIO(CPU_PROCHOT,	PIN(A, 3), GPIO_INPUT)		/* PCH_PROCHOT_ODL */
GPIO(EC_PCH_RTCRST,	PIN(B, 7), GPIO_INPUT)		/* EC_PCH_RTCRST TODO: Not used yet */

GPIO(DC_JACK_PRESENT_L,	PIN(7, 0), GPIO_INPUT)		/* DC Jack presence coming from +V3P3_A_KBC */

/*
 * I2C pins should be configured as inputs until I2C module is
 * initialized. This will avoid driving the lines unintentionally.
 */
GPIO(I2C0_SCL0,     PIN(B, 5), GPIO_ODR_HIGH)
GPIO(I2C0_SDA0,     PIN(B, 4), GPIO_ODR_HIGH)
GPIO(I2C0_SCL1,     PIN(B, 3), GPIO_ODR_HIGH)
GPIO(I2C0_SDA1,     PIN(B, 2), GPIO_ODR_HIGH)
GPIO(I2C1_SCL,      PIN(9, 0), GPIO_ODR_HIGH)
GPIO(I2C1_SDA,      PIN(8, 7), GPIO_ODR_HIGH)
GPIO(I2C2_SCL,      PIN(9, 2), GPIO_ODR_HIGH)
GPIO(I2C2_SDA,      PIN(9, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SCL,      PIN(D, 1), GPIO_ODR_HIGH)
GPIO(I2C3_SDA,      PIN(D, 0), GPIO_ODR_HIGH)

/* Type-C control GPIOs */
UNIMPLEMENTED(USB_C0_5V_EN)
UNIMPLEMENTED(USB_C1_5V_EN)

/* Unused pins 3.3V & Interruptable */
GPIO(NC_02,	PIN(0, 2), GPIO_INPUT)
GPIO(NC_04,	PIN(0, 4), GPIO_INPUT)

GPIO(NC_41,	PIN(4, 1), GPIO_INPUT)
GPIO(NC_42,	PIN(4, 2), GPIO_INPUT)

GPIO(NC_60,	PIN(6, 0), GPIO_INPUT)
GPIO(NC_61,	PIN(6, 1), GPIO_INPUT)
GPIO(NC_67,	PIN(6, 7), GPIO_INPUT)

GPIO(NC_71,	PIN(7, 1), GPIO_INPUT)
GPIO(NC_73,	PIN(7, 3), GPIO_INPUT)
GPIO(NC_74,	PIN(7, 4), GPIO_INPUT)

GPIO(NC_80,	PIN(8, 0), GPIO_INPUT)
GPIO(NC_82,	PIN(8, 2), GPIO_INPUT)
GPIO(NC_83,	PIN(8, 3), GPIO_INPUT)
GPIO(NC_84,	PIN(8, 4), GPIO_INPUT)

GPIO(NC_B1,	PIN(B, 1), GPIO_INPUT)

GPIO(NC_C0,	PIN(C, 0), GPIO_INPUT)
GPIO(NC_C2,	PIN(C, 2), GPIO_INPUT)
GPIO(NC_C3,	PIN(C, 3), GPIO_INPUT)
GPIO(NC_C4,	PIN(C, 4), GPIO_INPUT)
GPIO(NC_C5,	PIN(C, 5), GPIO_INPUT)
GPIO(NC_C7,	PIN(C, 7), GPIO_INPUT)

GPIO(NC_D3,	PIN(D, 3), GPIO_INPUT)

GPIO(NC_E7,	PIN(E, 7), GPIO_INPUT)

/* Unused pins: VSPI 3.3V or 1.8V & Interruptable */
GPIO(NC_94,	PIN(9, 4), GPIO_INPUT)
GPIO(NC_95,	PIN(9, 5), GPIO_INPUT)

GPIO(NC_A1,	PIN(A, 1), GPIO_INPUT)
GPIO(NC_A5,	PIN(A, 5), GPIO_INPUT)

GPIO(NC_B0,	PIN(B, 0), GPIO_INPUT)

/* Unused pins 3.3V & Non-Interruptable */
GPIO(NC_32,	PIN(3, 2), GPIO_INPUT)

GPIO(NC_66,	PIN(6, 6), GPIO_INPUT)

GPIO(NC_B6,	PIN(B, 6), GPIO_INPUT)

/* SMI/SCI pins unused for eSPI */
GPIO(NC_C6,	PIN(C, 6), GPIO_INPUT | GPIO_PULL_UP)
GPIO(NC_76,	PIN(7, 6), GPIO_INPUT | GPIO_PULL_UP)

/* eSPI: VHIF Unused pins 1.8V & Interruptable */
GPIO(NC_46,	PIN(4, 6), GPIO_INPUT)
GPIO(NC_47,	PIN(4, 7), GPIO_INPUT)

GPIO(NC_50,	PIN(5, 0), GPIO_INPUT)
GPIO(NC_51,	PIN(5, 1), GPIO_INPUT)
GPIO(NC_52,	PIN(5, 2), GPIO_INPUT)
GPIO(NC_53,	PIN(5, 3), GPIO_INPUT)
GPIO(NC_54,	PIN(5, 4), GPIO_INPUT)
GPIO(NC_55,	PIN(5, 5), GPIO_INPUT)
GPIO(NC_56,	PIN(5, 6), GPIO_INPUT)
GPIO(NC_57,	PIN(5, 7), GPIO_INPUT)

/* Alternate pins for UART */
ALTERNATE(PIN_MASK(6, 0x30), 1,  MODULE_UART,        0)    /* GPIO64/65    */

/* Alternate pins for I2C */
ALTERNATE(PIN_MASK(B, 0x0C), 1,  MODULE_I2C,         0)    /* I2C0SDA1/I2C0SCL1       GPIOB2/B3	   */
ALTERNATE(PIN_MASK(B, 0x30), 1,  MODULE_I2C,         0)    /* I2C0SDA0/I2C0SCL0       GPIOB4/B5	   */
ALTERNATE(PIN_MASK(8, 0x80), 1,  MODULE_I2C,         0)    /* I2C1SDA                 GPIO87       */
ALTERNATE(PIN_MASK(9, 0x07), 1,  MODULE_I2C,         0)    /* I2C1SCL/I2C2SDA/I2C2SCL GPIO90/91/92 */
ALTERNATE(PIN_MASK(D, 0x03), 1,  MODULE_I2C,         0)    /* I2C3SDA/I2C3SCL         GPIOD0/D1    */

/* Alternate pins for ADC/SPI/PWM/MFT */
ALTERNATE(PIN_MASK(4, 0x38), 1,  MODULE_ADC,         0)    /* ADC                     GPIO45/44/43 */
ALTERNATE(PIN_MASK(A, 0x0A), 1,  MODULE_SPI,         0)    /* SPIP_MOSI/SPIP_SCLK     GPIOA3/A1	   */
ALTERNATE(PIN_MASK(9, 0x20), 1,  MODULE_SPI,         0)    /* SPIP_MISO               GPIO95       */
ALTERNATE(PIN_MASK(C, 0x04), 3,  MODULE_PWM,         0)    /* PWM1 for PWM/KBLIGHT Test GPIOC2     */
/* Alternative functionality for FANS */
#ifdef CONFIG_FANS
ALTERNATE(PIN_MASK(C, 0x08), 7,  MODULE_PWM,         0)    /* PWM0 for PWM/FAN Test   GPIOC3       */
#if NPCX_TACH_SEL2
ALTERNATE(PIN_MASK(9, 0x08), 3,  MODULE_PWM,         0)    /* MFT-1/TA1_TACH1 for FAN GPIO93       */
#else
ALTERNATE(PIN_MASK(4, 0x01), 3,  MODULE_PWM,         0)    /* MFT-1/TA1_TACH1 for FAN Test GPIO40  */
#endif
#endif

/* Keyboard pins */
#define GPIO_KB_INPUT (GPIO_INPUT)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)

/* Keyboard Columns */
ALTERNATE(PIN_MASK(0, 0xE0), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT) /* GPIO05/06/07 */
ALTERNATE(PIN_MASK(1, 0xFF), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT) /* GPIO10/11/12/13/14/15/16/17 */
ALTERNATE(PIN_MASK(2, 0x03), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_OUTPUT) /* GPIO20/21 */

/* Keyboard Rows */
ALTERNATE(PIN_MASK(2, 0xFC), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* GPIO22/23/24/25/26/27 */
ALTERNATE(PIN_MASK(3, 0x03), 0,  MODULE_KEYBOARD_SCAN, GPIO_KB_INPUT) /* GPIO30/31 */
