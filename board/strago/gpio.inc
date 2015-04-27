/* -*- mode:c -*-
 *
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO_INT(LID_OPEN,          PORT(2),  7, GPIO_INT_BOTH_DSLEEP,                      lid_interrupt)          /* Lid switch */
GPIO_INT(AC_PRESENT,        PORT(3),  0, GPIO_INT_BOTH_DSLEEP,                      extpower_interrupt)     /* BC_ACOK / EC_ACIN - to know if battery or AC connected */
GPIO_INT(POWER_BUTTON_L,    PORT(3),  5, GPIO_INT_BOTH_DSLEEP,                      power_button_interrupt) /* Power button */
GPIO_INT(RSMRST_L_PGOOD,    PORT(6),  3, GPIO_INT_BOTH,                             power_signal_interrupt) /* RSMRST_N_PWRGD from power logic */
GPIO_INT(ALL_SYS_PGOOD,     PORT(13), 0, GPIO_INT_BOTH_DSLEEP,                      power_signal_interrupt) /* ALL_SYS_PWRGD from power logic */
GPIO_INT(PCH_SLP_S4_L,      PORT(20), 0, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO_INT(PCH_SLP_S3_L,      PORT(20), 6, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S3# signal from PCH */

GPIO(KBD_KSO2,          PORT(0),  1, GPIO_KB_OUTPUT_COL2)           /* Negative edge triggered irq. */

GPIO(USB_ILIM_SEL,      PORT(1),  3, GPIO_OUT_HIGH)                 /* USB current control */

GPIO(I2C_PORT0_SCL,     PORT(1),  5, GPIO_ODR_HIGH)
GPIO(I2C_PORT0_SDA,     PORT(1),  6, GPIO_ODR_HIGH)
GPIO(BOARD_ID2,         PORT(1),  7, GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(I2C_PORT1_SCL,     PORT(2),  2, GPIO_ODR_HIGH)
GPIO(I2C_PORT1_SDA,     PORT(2),  3, GPIO_ODR_HIGH)
GPIO(I2C_PORT2_SCL,     PORT(2),  0, GPIO_ODR_HIGH)
GPIO(I2C_PORT2_SDA,     PORT(2),  1, GPIO_ODR_HIGH)
GPIO(I2C_PORT3_SCL,     PORT(2),  4, GPIO_ODR_HIGH)
GPIO(I2C_PORT3_SDA,     PORT(2),  5, GPIO_ODR_HIGH)

GPIO(PCH_SCI_L,         PORT(2),  6, GPIO_ODR_HIGH)                 /* SCI output */

GPIO(VOLUME_UP,         PORT(3),  1, GPIO_INT_FALLING)              /* Volume up button */
GPIO(VOLUME_DOWN,       PORT(3),  4, GPIO_INT_FALLING)              /* Volume down button */
GPIO(USB2_PWR_EN,       PORT(3),  6, GPIO_OUT_LOW)                  /* Enable power for USB2 Port */

GPIO(ENTERING_RW,       PORT(4),  1, GPIO_OUT_LOW)                  /* Indicate when EC is entering RW code */
GPIO(PCH_SMI_L,         PORT(4),  4, GPIO_ODR_HIGH)                 /* SMI output */
GPIO(USB_OC1_L,         PORT(4),  5, GPIO_INT_FALLING)              /* DB2 BC1.2 over current signal to EC */
GPIO(DP_USB_C_HPD_Q,    PORT(4),  6, GPIO_OUT_HIGH)                 /* DP hot plug detect from EC to SOC */
GPIO(PWR_BTN_SELECT,    PORT(4),  7, GPIO_OUT_HIGH)                 /* HIGH in clamshell mode and LOW in tablet mode */

GPIO(OTG_SW_EN,		PORT(5),  0, GPIO_OUT_LOW)          /* */
GPIO(PCH_SUS_STAT_L,    PORT(5),  1, GPIO_INT_FALLING)              /* Signal to inform EC that SOC is entering low power state */
GPIO(NC_52,  		PORT(5),  2, GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(TRACKPAD_PWREN,    PORT(5),  3, GPIO_OUT_HIGH)                 /* Enable power for Track Pad */
GPIO(USB_OC0_L,         PORT(5),  5, GPIO_INT_FALLING)              /* Over current signal of the BC1.2 charger to EC */
GPIO(TEMP_SENSOR_1,     PORT(5),  6, GPIO_INPUT)                    /* EC_ADC0 */
GPIO(TEMP_SENSOR_2,     PORT(5),  7, GPIO_INPUT)                    /* EC_ADC0 */

GPIO(CHGR_PMON,         PORT(6),  0, GPIO_ANALOG)
GPIO(NC_61,             PORT(6),  1, GPIO_INPUT)                    /* NC */
GPIO(TEMP_SENSOR_3,     PORT(6),  2, GPIO_INPUT)                    /*  */
GPIO(USBPD_BST_OFF,     PORT(6),  4, GPIO_OUT_HIGH)                 /* USB PD Boost Enable */
GPIO(PCH_SYS_PWROK,     PORT(6),  5, GPIO_OUT_LOW)                  /* EC thinks everything is up and ready (DELAY_ALL_SYS_PWRGD) */
GPIO(PCH_WAKE_L,        PORT(6),  6, GPIO_ODR_HIGH)                 /* PCH wake pin */
GPIO(USB3_PWR_EN,       PORT(6),  7, GPIO_OUT_LOW)                  /* Enable power for USB3 Port */

GPIO(USB_CTL1,          PORT(10), 5, GPIO_OUT_HIGH)                 /* USB charging mode control */

GPIO(PCH_RCIN_L,        PORT(11), 0, GPIO_ODR_HIGH)                 /* Reset line to PCH (for 8042 emulation) */
GPIO(NC_115,            PORT(11), 5, GPIO_INPUT | GPIO_PULL_UP)     /* NC */

GPIO(USB_PD_EC_INT,     PORT(12), 2, GPIO_INT_BOTH)                 /* Interrupt from USB PD Controller to EC */
GPIO(STRAP_L,        	PORT(12), 3, GPIO_OUT_LOW)
GPIO(PERICOM_INT,    	PORT(12), 4, GPIO_INT_BOTH)                 /* */
GPIO(GYRO_INT2,         PORT(12), 7, GPIO_INT_FALLING)              /* Gyro sensor interrupt 2 to EC */

GPIO(EC_PLUG_DETECT,    PORT(13), 2, GPIO_INT_BOTH)
GPIO(BOARD_ID1,         PORT(15), 4, GPIO_INPUT | GPIO_PULL_UP)     /* Board Id 1 */
GPIO(NC_135,            PORT(13), 5, GPIO_INPUT | GPIO_PULL_UP)     /* NC */

GPIO(THERMAL_PROBE_EN_L,PORT(14), 0, GPIO_OUT_HIGH)
GPIO(PCH_RSMRST_L,      PORT(14), 3, GPIO_OUT_LOW)                  /* RSMRST_N to PCH */
GPIO(NC_145,            PORT(14), 5, GPIO_OUT_LOW | GPIO_PULL_UP)   /* NC */
GPIO(PVT_CS0,           PORT(14), 6, GPIO_ODR_HIGH)                 /* SPI PVT Chip select */
GPIO(ALS_INT,           PORT(14), 7, GPIO_INT_FALLING)              /* ALS sensor interrupt to EC */

GPIO(WLAN_OFF_L,        PORT(15), 0, GPIO_ODR_HIGH)                 /* Wireless LAN */
GPIO(CPU_PROCHOT,       PORT(15), 1, GPIO_OUT_LOW)
GPIO(KBD_IRQ_L,         PORT(15), 2, GPIO_ODR_HIGH)                 /* Negative edge triggered irq. */
GPIO(BOARD_ID0,         PORT(15), 4, GPIO_INPUT | GPIO_PULL_UP)     /* NC */
GPIO(CORE_PWROK,        PORT(15), 5, GPIO_ODR_HIGH)                 /* CORE_PWR_OK_R */
GPIO(LID_OPEN2,         PORT(15), 6, GPIO_INT_BOTH_DSLEEP)          /* LID_OPEN_OUT2_R */
GPIO(PCH_SUSPWRDNACK,   PORT(15), 7, GPIO_INT_FALLING)              /* PMC SUSPWRDNACK signal from SOC to EC */

GPIO(PCH_PWRBTN_L,      PORT(16), 0, GPIO_OUT_HIGH)                 /* Power button output to PCH */
GPIO(GYRO_INT1,         PORT(16), 1, GPIO_INT_FALLING)              /* Gyro sensor interrupt 1 to EC */
GPIO(CROSS_BAR_EN_NC,   PORT(16), 3, GPIO_INPUT | GPIO_PULL_UP)     /* NC */

GPIO(STARTUP_LATCH_SET, PORT(20), 1, GPIO_OUT_HIGH)                 /* Output from EC to POL signal of USB Type C Mux */
GPIO(EC_BL_DISABLE_L,   PORT(20), 2, GPIO_OUT_HIGH)                 /* EDP backligh disable signal from EC */
GPIO(SMC_SHUTDOWN,      PORT(20), 3, GPIO_OUT_LOW)                  /* Shutdown signal from EC to power sequencing PLD */
GPIO(CROSS_BAR_MODE_NC, PORT(20), 4, GPIO_INPUT | GPIO_PULL_UP)     /* NC */

GPIO(SUSPWRDNACK_SOC_EC,PORT(21), 0, GPIO_OUT_LOW)                  /* SUSPWRDNACK signal from MOIC device to EC */
GPIO(GPIO_3_EC,         PORT(21), 1, GPIO_OUT_LOW)                  /* Sleep SOIX signal from SOC to EC */

UNIMPLEMENTED(WP_L)

/* Alternate functions GPIO definition */
ALTERNATE(PORT(16),     0x24,   1,      MODULE_UART,            0)                      /* UART0 */
ALTERNATE(PORT(1),      0x60,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0: Battery Charger */
ALTERNATE(PORT(2),      0x3f,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C1: Temp Sensor / I2C2: SOC / I2C3: VNN */
ALTERNATE(PORT(0),      0xfc,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(1),      0x03,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(10),     0xd8,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(3),  	0x04,   3,	MODULE_KEYBOARD_SCAN, 	GPIO_KB_INPUT)
ALTERNATE(PORT(4),  	0x0d,   3, 	MODULE_KEYBOARD_SCAN,	GPIO_KB_INPUT)
ALTERNATE(PORT(12), 	0x60,   2, 	MODULE_KEYBOARD_SCAN, 	GPIO_KB_INPUT)
ALTERNATE(PORT(14), 	0x14,   3, 	MODULE_KEYBOARD_SCAN, 	GPIO_KB_INPUT)
ALTERNATE(PORT(1),  	0x10,   1, 	MODULE_LPC,           	0)                       /* 14: CLKRUN# */
ALTERNATE(PORT(11), 	0x9e,   1, 	MODULE_LPC,           	0)                       /* 111~114:LAD[0:3],  117:PCI_CLK */
ALTERNATE(PORT(11), 	0x40,   1, 	MODULE_LPC,           	GPIO_INT_BOTH)           /* 116: LRESET# */
ALTERNATE(PORT(12), 	0x01,   1, 	MODULE_LPC,           	0)                       /* 120: LFRAME# */
ALTERNATE(PORT(5),  	0x10,   1, 	MODULE_SPI,           	0)
ALTERNATE(PORT(16), 	0x10,   1, 	MODULE_SPI,           	0)
ALTERNATE(PORT(15), 	0x08,   1, 	MODULE_SPI,           	0)                      /* 153: CLK */
ALTERNATE(PORT(13),     0x48,   1,      MODULE_PWM_LED,         GPIO_OPEN_DRAIN)        /* 133: PWM0, 136: PWM1 */
ALTERNATE(PORT(14),     0x02,   1,      MODULE_PWM_LED,         GPIO_OPEN_DRAIN)        /* 141: PWM3 */

