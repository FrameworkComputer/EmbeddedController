/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Inputs with interrupt handlers are first for efficiency */
GPIO_INT(POWER_BUTTON_L,       K, 7, GPIO_INT_BOTH, power_button_interrupt) /* Power button */
GPIO_INT(LID_OPEN,             K, 5, GPIO_INT_BOTH, lid_interrupt)          /* Lid switch */
GPIO_INT(AC_PRESENT,           H, 3, GPIO_INT_BOTH, extpower_interrupt)     /* AC power present */
GPIO_INT(PCH_BKLTEN,           J, 3, GPIO_INT_BOTH, backlight_interrupt)    /* Backlight enable signal from PCH */
GPIO_INT(PCH_SLP_A_L,          G, 5, GPIO_INT_BOTH, power_signal_interrupt) /* SLP_A# signal from PCH */
GPIO_INT(PCH_SLP_ME_CSW_DEV_L, G, 4, GPIO_INT_BOTH, power_signal_interrupt) /* SLP_ME_CSW_DEV# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,         J, 0, GPIO_INT_BOTH, power_signal_interrupt) /* SLP_S3# signal from PCH */
GPIO_INT(PCH_SLP_S4_L,         J, 1, GPIO_INT_BOTH, power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO_INT(PCH_SLP_S5_L,         J, 2, GPIO_INT_BOTH, power_signal_interrupt) /* SLP_S5# signal from PCH */
GPIO_INT(PCH_SLP_SUS_L,        G, 3, GPIO_INT_BOTH, power_signal_interrupt) /* SLP_SUS# signal from PCH */
GPIO_INT(PCH_SUSWARN_L,        G, 2, GPIO_INT_BOTH, power_interrupt)        /* SUSWARN# signal from PCH */
GPIO_INT(PGOOD_1_5V_DDR,       K, 0, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +1.5V_DDR */
GPIO_INT(PGOOD_1_5V_PCH,       K, 1, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +1.5V_PCH */
GPIO_INT(PGOOD_1_8VS,          K, 3, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +1.8VS */
GPIO_INT(PGOOD_5VALW,          H, 0, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +5VALW */
GPIO_INT(PGOOD_CPU_CORE,       M, 3, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +CPU_CORE */
GPIO_INT(PGOOD_VCCP,           K, 2, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +VCCP */
GPIO_INT(PGOOD_VCCSA,          H, 1, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +VCCSA */
GPIO_INT(PGOOD_VGFX_CORE,      D, 2, GPIO_INT_BOTH, power_signal_interrupt) /* Power good on +VGFX_CORE */
GPIO_INT(RECOVERY_L,           H, 7, GPIO_INT_BOTH, switch_interrupt)       /* Recovery signal from servo */
GPIO_INT(WP,                   J, 4, GPIO_INT_BOTH, switch_interrupt)       /* Write protect input */
/* Other inputs */
GPIO(THERMAL_DATA_READY_L, B, 4, GPIO_INPUT)                   /* Data ready from I2C thermal sensor */
GPIO(BOARD_VERSION1,       H, 6, GPIO_INPUT)                   /* Board version stuffing resistor 1 */
GPIO(BOARD_VERSION2,       L, 6, GPIO_INPUT)                   /* Board version stuffing resistor 2 */
GPIO(BOARD_VERSION3,       L, 7, GPIO_INPUT)                   /* Board version stuffing resistor 3 */
GPIO(ONEWIRE,              H, 2, GPIO_INPUT)                   /* One-wire bus to adapter LED */
GPIO(USB1_STATUS_L,        E, 7, GPIO_INPUT)                   /* USB charger port 1 status output */
GPIO(USB2_STATUS_L,        E, 1, GPIO_INPUT)                   /* USB charger port 2 status output */

/* Outputs; all unasserted by default except for reset signals */
GPIO(CPU_PROCHOT,          F, 2, GPIO_OUT_LOW)  /* Force CPU to think it's overheated */
GPIO(ENABLE_1_5V_DDR,      H, 5, GPIO_OUT_LOW)  /* Enable +1.5V_DDR supply */
GPIO(ENABLE_5VALW,         K, 4, GPIO_OUT_HIGH) /* Enable +5V always on rail */
GPIO(ENABLE_BACKLIGHT,     H, 4, GPIO_OUT_LOW)  /* Enable backlight power */
GPIO(ENABLE_TOUCHPAD,      C, 6, GPIO_OUT_LOW)  /* Enable touchpad power */
GPIO(ENABLE_VCORE,         F, 7, GPIO_OUT_LOW)  /* Enable +CPU_CORE and +VGFX_CORE */
GPIO(ENABLE_VS,            G, 6, GPIO_OUT_LOW)  /* Enable VS power supplies */
GPIO(ENABLE_WLAN,          Q, 5, GPIO_OUT_LOW)  /* Enable WLAN module power (+3VS_WLAN) */
GPIO(ENTERING_RW,          J, 5, GPIO_OUT_LOW)  /* Indicate when EC is entering RW code */
GPIO(LIGHTBAR_RESET_L,     B, 1, GPIO_OUT_LOW)  /* Reset lightbar controllers */
GPIO(PCH_A20GATE,          Q, 6, GPIO_OUT_LOW)  /* A20GATE signal to PCH */
GPIO(PCH_DPWROK,           G, 0, GPIO_OUT_LOW)  /* DPWROK signal to PCH */

/*
 * HDA_SDO is technically an output, but we need to leave it as an
 * input until we drive it high.  So can't use open-drain (HI_Z).
 */
GPIO(PCH_HDA_SDO,          G, 1, GPIO_INPUT)    /* HDA_SDO signal to PCH; when high, ME ignores security descriptor */
GPIO(PCH_WAKE_L,           F, 0, GPIO_OUT_HIGH) /* Wake signal output to PCH */
GPIO(PCH_NMI_L,            M, 2, GPIO_OUT_HIGH) /* Non-maskable interrupt pin to PCH */
GPIO(PCH_PWRBTN_L,         G, 7, GPIO_OUT_HIGH) /* Power button output to PCH */
GPIO(PCH_PWROK,            F, 5, GPIO_OUT_LOW)  /* PWROK / APWROK signals to PCH */
GPIO(PCH_RCIN_L,           Q, 7, GPIO_ODR_HIGH) /* RCIN# signal to PCH */
GPIO(PCH_RSMRST_L,         F, 1, GPIO_OUT_LOW)  /* Reset PCH resume power plane logic */
GPIO(PCH_RTCRST_L,         F, 6, GPIO_ODR_HIGH) /* Reset PCH RTC well */
GPIO(PCH_SMI_L,            F, 4, GPIO_OUT_HIGH) /* System management interrupt to PCH */
GPIO(PCH_SRTCRST_L,        C, 7, GPIO_ODR_HIGH) /* Reset PCH ME RTC well */
GPIO(PCH_SUSACK_L,         F, 3, GPIO_OUT_HIGH) /* Acknowledge PCH SUSWARN# signal */
GPIO(RADIO_ENABLE_WLAN,    D, 0, GPIO_OUT_LOW)  /* Enable WLAN radio */
GPIO(RADIO_ENABLE_BT,      D, 1, GPIO_OUT_LOW)  /* Enable bluetooth radio */
GPIO(SPI_CS_L,             A, 3, GPIO_ODR_HIGH) /* SPI chip select */
GPIO(TOUCHSCREEN_RESET_L,  B, 0, GPIO_OUT_LOW)  /* Reset touch screen */
GPIO(USB1_CTL1,            E, 2, GPIO_OUT_LOW)  /* USB charger port 1 CTL1 output */
GPIO(USB1_CTL2,            E, 3, GPIO_OUT_LOW)  /* USB charger port 1 CTL2 output */
GPIO(USB1_CTL3,            E, 4, GPIO_OUT_LOW)  /* USB charger port 1 CTL3 output */
GPIO(USB1_ENABLE,          E, 5, GPIO_OUT_LOW)  /* USB charger port 1 enable */
GPIO(USB1_ILIM_SEL,        E, 6, GPIO_OUT_LOW)  /* USB charger port 1 ILIM_SEL output */
GPIO(USB2_CTL1,            D, 4, GPIO_OUT_LOW)  /* USB charger port 2 CTL1 output */
GPIO(USB2_CTL2,            D, 5, GPIO_OUT_LOW)  /* USB charger port 2 CTL2 output */
GPIO(USB2_CTL3,            D, 6, GPIO_OUT_LOW)  /* USB charger port 2 CTL3 output */
GPIO(USB2_ENABLE,          D, 7, GPIO_OUT_LOW)  /* USB charger port 2 enable */
GPIO(USB2_ILIM_SEL,        E, 0, GPIO_OUT_LOW)  /* USB charger port 2 ILIM_SEL output */

ALTERNATE(A, 0x03, 1,  MODULE_UART,        0)			/* UART0 */
ALTERNATE(A, 0x40, 3,  MODULE_I2C,         0)			/* I2C1 SCL */
ALTERNATE(A, 0x80, 3,  MODULE_I2C,         GPIO_OPEN_DRAIN)	/* I2C1 SDA */
ALTERNATE(B, 0x04, 3,  MODULE_I2C,         0)			/* I2C0 SCL */
ALTERNATE(B, 0x08, 3,  MODULE_I2C,         GPIO_OPEN_DRAIN)	/* I2C0 SDA */
ALTERNATE(B, 0x40, 3,  MODULE_I2C,         0)			/* I2C5 SCL */
ALTERNATE(B, 0x80, 3,  MODULE_I2C,         GPIO_OPEN_DRAIN)	/* I2C5 SDA */
ALTERNATE(C, 0x30, 2,  MODULE_UART,        0)			/* UART1 */
ALTERNATE(J, 0x40, 1,  MODULE_PECI,        0)			/* PECI Tx */
ALTERNATE(J, 0x80, 0,  MODULE_PECI,        GPIO_ANALOG)		/* PECI Rx */
ALTERNATE(K, 0x40, 1,  MODULE_PWM_KBLIGHT, 0)			/* FAN0PWM1 */
ALTERNATE(L, 0x3f, 15, MODULE_LPC,         0)			/* LPC */
ALTERNATE(M, 0x33, 15, MODULE_LPC,         0)			/* LPC */
ALTERNATE(M, 0xc0, 1,  MODULE_PWM_FAN,     0)			/* FAN0PWM0 */
