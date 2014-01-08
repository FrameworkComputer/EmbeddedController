/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Samus mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* Debug features */
#define CONFIG_CONSOLE_CMDHELP
#define CONFIG_TASK_PROFILING

#undef HEY_USE_BUILTIN_CLKRUN

/* Optional features */
#define CONFIG_ALS
#define CONFIG_ALS_ISL29035
#define CONFIG_BOARD_VERSION
#define CONFIG_CHIPSET_X86
#define CONFIG_CHIPSET_CAN_THROTTLE
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_DRIVER_DS2413
#define CONFIG_ONEWIRE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86

#define CONFIG_BACKLIGHT_REQ_GPIO GPIO_PCH_BL_EN
#define CONFIG_BATTERY_CHECK_CONNECTED
#define CONFIG_BATTERY_LINK
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24715
/* 10mOhm sense resitors. */
#define   CONFIG_CHARGER_SENSE_RESISTOR 10
#define   CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define   CONFIG_CHARGER_INPUT_CURRENT 2000
#define CONFIG_FANS 2
#define CONFIG_PECI_TJMAX 100
#define CONFIG_PWM
#define CONFIG_PWM_KBLIGHT
#define CONFIG_SWITCH_DEDICATED_RECOVERY
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP006
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_PP3300_DSW_GATED_EN
#define CONFIG_UART_HOST 2
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_VBOOT_HASH
#define CONFIG_WIRELESS

#ifndef __ASSEMBLER__

/* I2C ports */
#define I2C_PORT_BACKLIGHT 0
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0
#define I2C_PORT_ALS 1
#define I2C_PORT_CAPSENSE 1
#define I2C_PORT_LIGHTBAR 1
#define I2C_PORT_THERMAL 5

/* Backlight I2C device address */
#define I2C_ADDR_BACKLIGHT ((0x2C << 1) | I2C_FLAG_BIG_ENDIAN)

/* 13x8 keyboard scanner uses an entire GPIO bank for row inputs */
#define KB_SCAN_ROW_IRQ  LM4_IRQ_GPIOK
#define KB_SCAN_ROW_GPIO LM4_GPIO_K

/* Host connects to keyboard controller module via LPC */
#define HOST_KB_BUS_LPC

/* USB ports */
#define USB_PORT_COUNT 2

/* GPIO signal definitions. */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_POWER_BUTTON_L = 0,   /* Power button */
	GPIO_LID_OPEN,             /* Lid switch */
	GPIO_AC_PRESENT,           /* AC power present */
	GPIO_PCH_SLP_S0_L,         /* SLP_S0# signal from PCH */
	GPIO_PCH_SLP_S3_L,         /* SLP_S3# signal from PCH */
	GPIO_PCH_SLP_S5_L,         /* SLP_S5# signal from PCH */
	GPIO_PCH_SLP_SUS_L,        /* SLP_SUS# signal from PCH */
	GPIO_PCH_SUSWARN_L,        /* SUSWARN# signal from PCH */
	GPIO_PP1050_PGOOD,         /* Power good on 1.05V */
	GPIO_PP1200_PGOOD,         /* Power good on 1.2V (DRAM) */
	GPIO_PP1800_PGOOD,         /* Power good on 1.8V (DRAM) */
	GPIO_VCORE_PGOOD,          /* Power good on core VR */
	GPIO_RECOVERY_L,           /* Recovery signal from servo */
	GPIO_WP_L,                 /* Write protect input */
	GPIO_PCH_BL_EN,            /* PCH backlight input */

	/* Other inputs */
	GPIO_BOARD_VERSION1,       /* Board version stuffing resistor 1 */
	GPIO_BOARD_VERSION2,       /* Board version stuffing resistor 2 */
	GPIO_BOARD_VERSION3,       /* Board version stuffing resistor 3 */
	GPIO_CPU_PGOOD,            /* Power good to the CPU */
	GPIO_ONEWIRE,              /* One-wire bus to adapter LED */
	GPIO_THERMAL_DATA_READY_L, /* From thermal sensor */
	GPIO_USB1_OC_L,            /* USB port overcurrent warning */
	GPIO_USB1_STATUS_L,        /* USB charger port 1 status output */
	GPIO_USB2_OC_L,            /* USB port overcurrent warning */
	GPIO_USB2_STATUS_L,        /* USB charger port 2 status output */
	GPIO_CAPSENSE_INT_L,       /* Capsense interrupt (through EC_WAKE_L) */

	/* Outputs */
	GPIO_CPU_PROCHOT,          /* Force CPU to think it's overheated */
	GPIO_PP1200_EN,            /* Enable 1.20V supply */
	GPIO_PP3300_DSW_EN,        /* Enable 3.3V DSW rail */
	GPIO_PP3300_DSW_GATED_EN,  /* Enable 3.3V Gated DSW and core VDD */
	GPIO_PP3300_LTE_EN,        /* Enable LTE radio */
	GPIO_PP3300_WLAN_EN,       /* Enable WiFi power */
	GPIO_PP1050_EN,            /* Enable 1.05V regulator */
	GPIO_PP5000_USB_EN,        /* Enable USB power */
	GPIO_PP5000_EN,            /* Enable 5V supply */
	GPIO_PP1800_EN,            /* Enable 1.8V supply */
	GPIO_SYS_PWROK,            /* EC thinks everything is up and ready */
	GPIO_WLAN_OFF_L,           /* Disable WiFi radio */

	GPIO_ENABLE_BACKLIGHT,     /* Enable backlight power */
	GPIO_ENABLE_TOUCHPAD,      /* Enable touchpad power */
	GPIO_ENTERING_RW,          /* Indicate when EC is entering RW code */
	GPIO_LIGHTBAR_RESET_L,     /* Reset lightbar controllers */
	GPIO_PCH_DPWROK,           /* Indicate when VccDSW is good */

	GPIO_PCH_HDA_SDO,          /* HDA_SDO signal to PCH; when high, ME
				    * ignores security descriptor */
	GPIO_PCH_WAKE_L,           /* Wake signal from EC to PCH */
	GPIO_PCH_NMI_L,            /* Non-maskable interrupt pin to PCH */
	GPIO_PCH_PWRBTN_L,         /* Power button output to PCH */
	GPIO_PCH_PWROK,            /* PWROK / APWROK signals to PCH */
	GPIO_PCH_RCIN_L,           /* RCIN# line to PCH (for 8042 emulation) */
	GPIO_PCH_SYS_RST_L,        /* Reset PCH resume power plane logic */
	GPIO_PCH_SMI_L,            /* System management interrupt to PCH */
	GPIO_TOUCHSCREEN_RESET_L,  /* Reset touch screen */
	GPIO_PCH_ACOK,             /* AC present signal buffered to PCH */
#ifndef HEY_USE_BUILTIN_CLKRUN
	GPIO_LPC_CLKRUN_L,         /* Dunno. Probably important, though. */
#endif
	GPIO_USB1_CTL1,            /* USB charger port 1 CTL1 output */
	GPIO_USB1_CTL2,            /* USB charger port 1 CTL2 output */
	GPIO_USB1_CTL3,            /* USB charger port 1 CTL3 output */
	GPIO_USB1_ENABLE,          /* USB charger port 1 enable */
	GPIO_USB1_ILIM_SEL,        /* USB charger port 1 ILIM_SEL output */
	GPIO_USB2_CTL1,            /* USB charger port 2 CTL1 output */
	GPIO_USB2_CTL2,            /* USB charger port 2 CTL2 output */
	GPIO_USB2_CTL3,            /* USB charger port 2 CTL3 output */
	GPIO_USB2_ENABLE,          /* USB charger port 2 enable */
	GPIO_USB2_ILIM_SEL,        /* USB charger port 2 ILIM_SEL output */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* x86 signal definitions */
enum x86_signal {
	X86_PGOOD_PP1050 = 0,
	X86_PGOOD_PP1200,
	X86_PGOOD_PP1800,
	X86_PGOOD_VCORE,

	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S5_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_SUSWARN_DEASSERTED,

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum adc_channel {
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,
	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,
	/* BAT_TEMP */
	ADC_CH_BAT_TEMP,

	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,

	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum temp_sensor_id {
	/* CPU die temperature via PECI */
	TEMP_SENSOR_CPU_PECI,
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL,
	/* TMP006 U40, die/object temperature near battery charger */
	TEMP_SENSOR_I2C_U40_DIE,
	TEMP_SENSOR_I2C_U40_OBJECT,
	/* TMP006 U41, die/object temperature near CPU */
	TEMP_SENSOR_I2C_U41_DIE,
	TEMP_SENSOR_I2C_U41_OBJECT,
	/* TMP006 U42, die/object temperature left side of C-case */
	TEMP_SENSOR_I2C_U42_DIE,
	TEMP_SENSOR_I2C_U42_OBJECT,
	/* TMP006 U43, die/object temperature right side of C-case */
	TEMP_SENSOR_I2C_U43_DIE,
	TEMP_SENSOR_I2C_U43_OBJECT,
	/* TMP006 U115, die/object temperature right side of D-case */
	TEMP_SENSOR_I2C_U115_DIE,
	TEMP_SENSOR_I2C_U115_OBJECT,
	/* TMP006 U116, die/object temperature left side of D-case */
	TEMP_SENSOR_I2C_U116_DIE,
	TEMP_SENSOR_I2C_U116_OBJECT,

	TEMP_SENSOR_COUNT
};

/* The number of TMP006 sensor chips on the board. */
#define TMP006_COUNT 6

/* Light sensors attached to the EC. */
enum als_id {
	ALS_ISL29035 = 0,

	ALS_COUNT,
};

/* Known board versions for system_get_board_version(). */
enum board_version {
	BOARD_VERSION_PROTO1 = 0,
	BOARD_VERSION_EVT = 1,
};

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WWAN GPIO_PP3300_LTE_EN
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
