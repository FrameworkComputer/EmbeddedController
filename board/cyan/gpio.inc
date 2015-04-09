/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

GPIO(LID_OPEN,          PORT(2),  7, GPIO_INT_BOTH_DSLEEP,                      lid_interrupt)          /* Lid switch */
GPIO(AC_PRESENT,        PORT(3),  0, GPIO_INT_BOTH_DSLEEP,                      extpower_interrupt)     /* BC_ACOK / EC_ACIN - to know if battery or AC connected */
GPIO(WP_L,              PORT(3),  3, GPIO_INT_BOTH,                             switch_interrupt)       /* Write protect input */
GPIO(POWER_BUTTON_L,    PORT(3),  5, GPIO_INT_BOTH_DSLEEP,                      power_button_interrupt) /* Power button */
GPIO(RSMRST_L_PGOOD,    PORT(6),  3, GPIO_INT_BOTH,                             power_signal_interrupt) /* RSMRST_N_PWRGD from power logic */
GPIO(ALL_SYS_PGOOD,     PORT(13), 0, GPIO_INT_BOTH_DSLEEP,                      power_signal_interrupt) /* ALL_SYS_PWRGD from power logic */
GPIO(PCH_SLP_S4_L,      PORT(20), 0, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S4# signal from PCH */
GPIO(PCH_SLP_S3_L,      PORT(20), 6, GPIO_INT_BOTH_DSLEEP | GPIO_PULL_UP,       power_signal_interrupt) /* SLP_S3# signal from PCH */

GPIO(NC_012,            PORT(1),  2, GPIO_INPUT | GPIO_PULL_UP,                 NULL)                   /* NC */
GPIO(USB_ILIM_SEL,      PORT(1),  3, GPIO_OUT_HIGH,                             NULL)                   /* USB current control */
GPIO(I2C_PORT0_SCL,     PORT(1),  5, GPIO_ODR_HIGH,                             NULL)
GPIO(I2C_PORT0_SDA,     PORT(1),  6, GPIO_ODR_HIGH,                             NULL)

GPIO(I2C_PORT1_SCL,     PORT(2),  2, GPIO_ODR_HIGH,                             NULL)
GPIO(I2C_PORT1_SDA,     PORT(2),  3, GPIO_ODR_HIGH,                             NULL)
GPIO(PCH_SCI_L,         PORT(2),  6, GPIO_ODR_HIGH,                             NULL)                   /* SCI output */

GPIO(VOLUME_UP,         PORT(3),  1, GPIO_INT_FALLING,                          NULL)                   /* Volume up button */
GPIO(VOLUME_DOWN,       PORT(3),  4, GPIO_INT_FALLING,                          NULL)                   /* Volume down button */
GPIO(USB2_PWR_EN,       PORT(3),  6, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for USB2 Port */

GPIO(ENTERING_RW,       PORT(4),  1, GPIO_OUT_LOW,                              NULL)                   /* Indicate when EC is entering RW code */
GPIO(PCH_SMI_L,         PORT(4),  4, GPIO_ODR_HIGH,                             NULL)                   /* SMI output */
GPIO(USB_OC1_L,         PORT(4),  5, GPIO_INT_FALLING,                          NULL)                   /* DB2 BC1.2 over current signal to EC */
GPIO(DP_USB_C_HPD_Q,    PORT(4),  6, GPIO_OUT_HIGH,                             NULL)                   /* DP hot plug detect from EC to SOC */
GPIO(VBATA_VR_HOT_N,    PORT(4),  7, GPIO_INT_FALLING,                          NULL)                   /* PROCHOT signal of VBATA VR connected to EC */

GPIO(VNN_I2C3_ALERT_N,  PORT(5),  0, GPIO_INT_FALLING,                          NULL)                   /* Alert signal from VNN VR to EC */
GPIO(PCH_SUS_STAT_L,    PORT(5),  1, GPIO_INT_FALLING,                          NULL)                   /* Signal to inform EC that SOC is entering low power state */
GPIO(TOUCHPANEL_PWREN,  PORT(5),  2, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for Touch Panel */
GPIO(TRACKPAD_PWREN,    PORT(5),  3, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for Track Pad */
GPIO(USB_OC0_L,         PORT(5),  5, GPIO_INT_FALLING,                          NULL)                   /* Over current signal of the BC1.2 charger to EC */
GPIO(EC_ADC0,           PORT(5),  6, GPIO_ANALOG,                               NULL)                   /* EC_ADC0 */
GPIO(EC_ADC1,           PORT(5),  7, GPIO_ANALOG,                               NULL)                   /* EC_ADC1 */

GPIO(CHGR_PMON,         PORT(6),  0, GPIO_ANALOG,                               NULL)
GPIO(WIFI_PWREN,        PORT(6),  1, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for WiFi */
GPIO(BATT_EN_L,         PORT(6),  2, GPIO_INPUT,                                NULL)                   /* Will be NC */
GPIO(SW_OPEN_EC,        PORT(6),  4, GPIO_INT_BOTH,                             NULL)                   /* Signal from USB Charger to EC to indicate if the isolation switch is open or close */
GPIO(PCH_SYS_PWROK,     PORT(6),  5, GPIO_OUT_LOW,                              NULL)                   /* EC thinks everything is up and ready (DELAY_ALL_SYS_PWRGD) */
GPIO(PCH_WAKE_L,        PORT(6),  6, GPIO_ODR_HIGH,                             NULL)                   /* PCH wake pin */
GPIO(USB3_PWR_EN,       PORT(6),  7, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for USB3 Port */

GPIO(USB_CTL1,          PORT(10), 5, GPIO_OUT_HIGH,                             NULL)                   /* USB charging mode control */

GPIO(PCH_RCIN_L,        PORT(11), 0, GPIO_ODR_HIGH,                             NULL)                   /* Reset line to PCH (for 8042 emulation) */
GPIO(NC_115,            PORT(11), 5, GPIO_INPUT | GPIO_PULL_UP,                 NULL)                   /* NC */

GPIO(USB_PD_EC_INT,     PORT(12), 2, GPIO_INT_BOTH,                             NULL)                   /* Interrupt from USB PD Controller to EC */
GPIO(EC_WAKE_L,         PORT(12), 3, GPIO_INT_BOTH_DSLEEP,                      NULL)
GPIO(USB_CHG_DET_EC,    PORT(12), 4, GPIO_INT_BOTH,                             NULL)                   /* Signal from USB Charger to EC to indicate charger has been detected */
GPIO(GYRO_INT2,         PORT(12), 7, GPIO_INT_FALLING,                          NULL)                   /* Gyro sensor interrupt 2 to EC */

GPIO(EC_PLUG_DETECT,    PORT(13), 2, GPIO_INT_BOTH,                             NULL)
GPIO(BAT_LED0_L,        PORT(13), 3, GPIO_ODR_HIGH,                             NULL)                   /* Battery charging LED - blue */
GPIO(WWAN_PWREN,        PORT(13), 5, GPIO_OUT_HIGH,                             NULL)                   /* Enable power for WWAN - PROBE_DETECT_L */
GPIO(BAT_LED1_L,        PORT(13), 6, GPIO_ODR_HIGH,                             NULL)                   /* Battery charging LED - orange */

GPIO(THERMAL_PROBE_EN_L,PORT(14), 0, GPIO_OUT_HIGH,                             NULL)
GPIO(PWR_LED0_L,        PORT(14), 1, GPIO_ODR_HIGH,                             NULL)                   /* Power LED - blue */
GPIO(PCH_RSMRST_L,      PORT(14), 3, GPIO_OUT_LOW,                              NULL)                   /* RSMRST_N to PCH */
GPIO(PWR_LED1_L,        PORT(14), 5, GPIO_ODR_HIGH,                             NULL)                   /* Power LED - orange */
GPIO(PVT_CS0,           PORT(14), 6, GPIO_ODR_HIGH,                             NULL)                   /* SPI PVT Chip select */
GPIO(ALS_INT,           PORT(14), 7, GPIO_INT_FALLING,                          NULL)                   /* ALS sensor interrupt to EC */

GPIO(WLAN_OFF_L,        PORT(15), 0, GPIO_ODR_HIGH,                             NULL)                   /* Wireless LAN */
GPIO(CPU_PROCHOT,       PORT(15), 1, GPIO_OUT_LOW,                              NULL)
GPIO(KBD_IRQ_L,         PORT(15), 2, GPIO_ODR_HIGH,                             NULL)                   /* Negative edge triggered irq. */
GPIO(NC_154,            PORT(15), 4, GPIO_INPUT | GPIO_PULL_UP,                 NULL)                   /* NC */
GPIO(CORE_PWROK,        PORT(15), 5, GPIO_ODR_HIGH,                             NULL)                   /* CORE_PWR_OK_R */
GPIO(LID_OPEN2,         PORT(15), 6, GPIO_INT_BOTH_DSLEEP,                      NULL)                   /* LID_OPEN_OUT2_R */
GPIO(PCH_SUSPWRDNACK,   PORT(15), 7, GPIO_INT_FALLING,                          NULL)                   /* PMC SUSPWRDNACK signal from SOC to EC */

GPIO(PCH_PWRBTN_L,      PORT(16), 0, GPIO_OUT_HIGH,                             NULL)                   /* Power button output to PCH */
GPIO(GYRO_INT1,         PORT(16), 1, GPIO_INT_FALLING,                          NULL)                   /* Gyro sensor interrupt 1 to EC */
GPIO(MUX_CROSS_BAR_EN,  PORT(16), 3, GPIO_OUT_LOW,                              NULL)                   /* Enable signal of the USB Type C Mux */

GPIO(MUX_CROSS_BAR_POL, PORT(20), 1, GPIO_OUT_LOW,                              NULL)                   /* Output from EC to POL signal of USB Type C Mux */
GPIO(EC_BL_DISABLE_L,   PORT(20), 2, GPIO_OUT_HIGH,                             NULL)                   /* EDP backligh disable signal from EC */
GPIO(SMC_SHUTDOWN,      PORT(20), 3, GPIO_OUT_LOW,                              NULL)                   /* Shutdown signal from EC to power sequencing PLD */
GPIO(MUX_CROSS_BAR_MODE,PORT(20), 4, GPIO_OUT_LOW,                              NULL)                   /* Output from EC - Type C Mux mode select */

GPIO(SUSPWRDNACK_SOC_EC,PORT(21), 0, GPIO_OUT_LOW,                              NULL)                   /* SUSPWRDNACK signal from EC to MOIC device */
GPIO(PCH_SLP_SX_L,      PORT(21), 1, GPIO_INT_BOTH_DSLEEP,                      NULL)                   /* Sleep SOIX signal from SOC to EC */

/* Alternate functions GPIO definition */
ALTERNATE(PORT(16),     0x24,   1,      MODULE_UART,            0)                      /* UART0 */

ALTERNATE(PORT(1),      0x60,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0: Battery Charger */
ALTERNATE(PORT(2),      0x3f,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C1: Temp Sensor / I2C2: SOC / I2C3: VNN */
/* PORT SEL [3:0] has to be set to 1 to access this port 1 of controller 0 */
ALTERNATE(PORT(13),     0x10,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0 - Port 1 : Accel SCL */
ALTERNATE(PORT(1),      0x80,   2,      MODULE_I2C,             GPIO_PULL_UP)           /* I2C0 - Port 1 : Accel SDA */

ALTERNATE(PORT(0),      0xfc,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(1),      0x03,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
ALTERNATE(PORT(10)   ,  0xd8,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT)
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
 GPIO(KBD_KSO2,         PORT(0),1,      GPIO_KB_OUTPUT_COL2,    NULL)                   /* Negative edge triggered irq. */
#else
 ALTERNATE(PORT(0),     0x02,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_OUTPUT_COL2)
#endif
ALTERNATE(PORT(3),      0x04,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PORT(4),      0x0d,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PORT(12),     0x60,   2,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)
ALTERNATE(PORT(14),     0x14,   3,      MODULE_KEYBOARD_SCAN,   GPIO_KB_INPUT)

ALTERNATE(PORT(1),      0x10,   1,      MODULE_LPC,             0)                      /* 14: CLKRUN# */
ALTERNATE(PORT(11),     0x9e,   1,      MODULE_LPC,             0)                      /* 111~114: LAD[0:3], 117: PCI_CLK */
ALTERNATE(PORT(11),     0x40,   1,      MODULE_LPC,             GPIO_INT_BOTH)          /* 116: LRESET# */
ALTERNATE(PORT(12),     0x01,   1,      MODULE_LPC,             0)                      /* 120: LFRAME# */

ALTERNATE(PORT(5),      0x10,   1,      MODULE_SPI,             0)                      /* 54: MOSI */
ALTERNATE(PORT(16),     0x10,   1,      MODULE_SPI,             0)                      /* 164: MISO */
ALTERNATE(PORT(15),     0x08,   1,      MODULE_SPI,             0)                      /* 153: CLK */
