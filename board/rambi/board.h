/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Rambi mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_BACKLIGHT_LID
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24715
#define CONFIG_CHARGER_INPUT_CURRENT 3420   /* 65 W adapter, 19 V, 3.42 A */
#define CONFIG_CHARGER_SENSE_RESISTOR 10    /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* Input senso resistor, mOhm */
#define CONFIG_CHIPSET_BAYTRAIL
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_CHIPSET_X86
#define CONFIG_CMD_GSV
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#undef  CONFIG_PECI
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_PWM
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_G781
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_PP3300_DX_EN
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_WIRELESS

#ifndef __ASSEMBLER__

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0
#define I2C_PORT_THERMAL 5

/* 13x8 keyboard scanner uses an entire GPIO bank for row inputs */
#define KB_SCAN_ROW_IRQ  LM4_IRQ_GPIOK
#define KB_SCAN_ROW_GPIO LM4_GPIO_K

/* Host connects to keyboard controller module via LPC */
#define HOST_KB_BUS_LPC

/* USB ports */
#define USB_PORT_COUNT 2

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WWAN GPIO_PP3300_LTE_EN
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN

/* GPIO signal definitions. */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_POWER_BUTTON_L = 0,   /* Power button */
	GPIO_LID_OPEN,             /* Lid switch */
	GPIO_AC_PRESENT,           /* AC power present */
	GPIO_PCH_SLP_S3_L,         /* SLP_S3# signal from PCH */
	GPIO_PCH_SLP_S4_L,         /* SLP_S4# signal from PCH */
	GPIO_PP1050_PGOOD,         /* Power good on 1.05V */
	GPIO_PP3300_PCH_PGOOD,     /* Power good on 3.3V (PCH supply) */
	GPIO_PP5000_PGOOD,         /* Power good on 5V */
	GPIO_S5_PGOOD,             /* Power good on S5 supplies */
	GPIO_VCORE_PGOOD,          /* Power good on core VR */
	GPIO_WP_L,                 /* Write protect input */
	GPIO_JTAG_TCK,             /* JTAG clock input */
	GPIO_UART0_RX,             /* UART0 RX input */

	/* Other inputs */
	GPIO_BOARD_VERSION1,       /* Board version stuffing resistor 1 */
	GPIO_BOARD_VERSION2,       /* Board version stuffing resistor 2 */
	GPIO_BOARD_VERSION3,       /* Board version stuffing resistor 3 */
	GPIO_PCH_SLP_SX_L,         /* SLP_S0IX# signal from PCH */
	GPIO_PCH_SUS_STAT_L,       /* SUS_STAT# signal from PCH */
	GPIO_PCH_SUSPWRDNACK,      /* SUSPWRDNACK signal from PCH */
	GPIO_PP1000_S0IX_PGOOD,    /* Power good on 1.00V (S0iX supplies) */
	GPIO_USB1_OC_L,            /* USB port overcurrent warning */
	GPIO_USB2_OC_L,            /* USB port overcurrent warning */

	/* Outputs */
	GPIO_CPU_PROCHOT,          /* Force CPU to think it's overheated */
	GPIO_ENABLE_BACKLIGHT,     /* Enable backlight power */
	GPIO_ENABLE_TOUCHPAD,      /* Enable touchpad power */
	GPIO_ENTERING_RW,          /* Indicate when EC is entering RW code */
	GPIO_LPC_CLKRUN_L,         /* Request that PCH drive LPC clock */
	GPIO_PCH_CORE_PWROK,       /* Indicate core well power is stable */
	GPIO_PCH_PWRBTN_L,         /* Power button output to PCH */
	GPIO_PCH_RCIN_L,           /* Reset line to PCH (for 8042 emulation) */
	GPIO_PCH_RSMRST_L,         /* Reset PCH resume power plane logic */
	GPIO_PCH_SMI_L,            /* System management interrupt to PCH */
	GPIO_PCH_SOC_OVERRIDE_L,   /* SOC override signal to PCH; when high, ME
				    * ignores security descriptor */
	GPIO_PCH_SYS_PWROK,        /* EC thinks everything is up and ready */
	GPIO_PCH_WAKE_L,           /* Wake signal from EC to PCH */
	GPIO_PP1350_EN,            /* Enable 1.35V supply */
	GPIO_PP3300_DX_EN,         /* Enable power to lots of peripherals */
	GPIO_PP3300_LTE_EN,        /* Enable LTE radio */
	GPIO_PP3300_WLAN_EN,       /* Enable WiFi power */
	GPIO_PP5000_EN,            /* Enable 5V supply */
	GPIO_PPSX_EN,              /* Enable PP1350_PCH_SX, PP1000_PCH_SX */
	GPIO_SUSP_VR_EN,           /* Enable 1.05V regulator */
	GPIO_TOUCHSCREEN_RESET_L,  /* Reset touch screen */
	GPIO_USB_CTL1,             /* USB control signal 1 to both ports */
	GPIO_USB_ILIM_SEL,         /* USB current limit to both ports */
	GPIO_USB1_ENABLE,          /* USB port 1 output power enable */
	GPIO_USB2_ENABLE,          /* USB port 2 output power enable */
	GPIO_VCORE_EN,             /* Enable core power supplies */
	GPIO_WLAN_OFF_L,           /* Disable WiFi radio */
	GPIO_PCH_SCI_L,            /* Assert SCI to PCH */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* x86 signal definitions */
enum x86_signal {
	X86_PGOOD_PP1050 = 0,
	X86_PGOOD_PP3300_PCH,
	X86_PGOOD_PP5000,
	X86_PGOOD_S5,
	X86_PGOOD_VCORE,
	X86_PGOOD_PP1000_S0IX,
	X86_PCH_SLP_S3n_DEASSERTED,
	X86_PCH_SLP_S4n_DEASSERTED,
	X86_PCH_SLP_SXn_DEASSERTED,
	X86_PCH_SUS_STATn,
	X86_PCH_SUSPWRDNACK,

	/* Number of X86 signals */
	X86_SIGNAL_COUNT
};

enum adc_channel {
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,

	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_LED_GREEN,
	PWM_CH_LED_RED,

	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum temp_sensor_id {
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL = 0,

	/* G781 internal and external sensors */
	TEMP_SENSOR_I2C_G781_INTERNAL,
	TEMP_SENSOR_I2C_G781_EXTERNAL,

	TEMP_SENSOR_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
