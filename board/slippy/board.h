/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Slippy mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_BACKLIGHT_X86
#define CONFIG_BATTERY_CHECK_CONNECTED
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24707A
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHIPSET_HASWELL
#define CONFIG_CHIPSET_X86
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_SLIPPY
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_PWM_FAN
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_G781
#define CONFIG_UART_HOST 2
#define CONFIG_USB_PORT_POWER_DUMB
#define CONFIG_WIRELESS

#ifndef __ASSEMBLER__

/* Module IDs */
/* TODO(rspangler): use this in place of enum console_channel as well */
enum module_id {
	MODULE_I2C,
	MODULE_LPC,
	MODULE_PECI,
	MODULE_PWM_FAN,
	MODULE_UART,
};

/* PWM channels */
#define FAN_CH_CPU         2  /* CPU fan */
#define FAN_CH_BL_DISPLAY  4  /* LVDS backlight (from PCH, cleaned by EC) */

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0
#define I2C_PORT_THERMAL 5
/* There are only two I2C ports used because battery and charger share a port */
#define I2C_PORTS_USED 2

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
	GPIO_PCH_BKLTEN,           /* Backlight enable signal from PCH */
	GPIO_PCH_SLP_S0_L,         /* SLP_S0# signal from PCH */
	GPIO_PCH_SLP_S3_L,         /* SLP_S3# signal from PCH */
	GPIO_PCH_SLP_S5_L,         /* SLP_S5# signal from PCH */
	GPIO_PCH_SLP_SUS_L,        /* SLP_SUS# signal from PCH */
	GPIO_PP1050_PGOOD,         /* Power good on 1.05V */
	GPIO_PP1350_PGOOD,         /* Power good on 1.35V (DRAM) */
	GPIO_PP5000_PGOOD,         /* Power good on 5V */
	GPIO_VCORE_PGOOD,          /* Power good on core VR */
	GPIO_PCH_EDP_VDD_EN,       /* PCH wants EDP enabled */
	GPIO_RECOVERY_L,           /* Recovery signal from servo */
	GPIO_WP_L,                 /* Write protect input */

	/* Other inputs */
	GPIO_FAN_ALERT_L,          /* From thermal sensor */
	GPIO_PCH_SUSWARN_L,        /* SUSWARN# signal from PCH */
	GPIO_USB1_OC_L,            /* USB port overcurrent warning */
	GPIO_USB2_OC_L,            /* USB port overcurrent warning */
	GPIO_BOARD_VERSION1,       /* Board version stuffing resistor 1 */
	GPIO_BOARD_VERSION2,       /* Board version stuffing resistor 2 */
	GPIO_BOARD_VERSION3,       /* Board version stuffing resistor 3 */
	GPIO_CPU_PGOOD,            /* Power good to the CPU */
	GPIO_BAT_DETECT_L,         /* Battery detect. Repurposed BAT_TEMP */

	/* Outputs */
	GPIO_CPU_PROCHOT,          /* Force CPU to think it's overheated */
	GPIO_PP1350_EN,            /* Enable 1.35V supply */
	GPIO_PP3300_DSW_GATED_EN,  /* Enable DSW rails */
	GPIO_PP3300_DX_EN,         /* Enable power to lots of peripherals */
	GPIO_PP3300_LTE_EN,        /* Enable LTE radio */
	GPIO_PP3300_WLAN_EN,       /* Enable WiFi power */
	GPIO_SUSP_VR_EN,           /* Enable 1.05V regulator */
	GPIO_VCORE_EN,             /* Stuffing option - not connected */
	GPIO_PP5000_EN,            /* Enable 5V supply */
	GPIO_SYS_PWROK,            /* EC thinks everything is up and ready */
	GPIO_WLAN_OFF_L,           /* Disable WiFi radio */
	GPIO_CHARGE_L,             /* Allow battery to charge when on AC */

	GPIO_ENABLE_BACKLIGHT,     /* Enable backlight power */
	GPIO_ENABLE_TOUCHPAD,      /* Enable touchpad power */
	GPIO_ENTERING_RW,          /* Indicate when EC is entering RW code */
	GPIO_PCH_DPWROK,           /* Indicate when VccDSW is good */

	GPIO_PCH_HDA_SDO,          /* HDA_SDO signal to PCH; when high, ME
				    * ignores security descriptor */
	GPIO_PCH_WAKE_L,           /* Wake signal from EC to PCH */
	GPIO_PCH_NMI_L,            /* Non-maskable interrupt pin to PCH */
	GPIO_PCH_PWRBTN_L,         /* Power button output to PCH */
	GPIO_PCH_PWROK,            /* PWROK / APWROK signals to PCH */
	GPIO_PCH_RCIN_L,           /* RCIN# line to PCH (for 8042 emulation) */
	GPIO_PCH_RSMRST_L,         /* Reset PCH resume power plane logic */
	GPIO_PCH_SMI_L,            /* System management interrupt to PCH */
	GPIO_TOUCHSCREEN_RESET_L,  /* Reset touch screen */
	GPIO_EC_EDP_VDD_EN,        /* Enable EDP (passthru from PCH) */
	GPOI_LPC_CLKRUN_L,         /* Dunno. Probably important, though. */

	GPIO_USB1_ENABLE,          /* USB port 1 output power enable */
	GPIO_USB2_ENABLE,          /* USB port 2 output power enable */

	GPIO_PCH_SUSACK_L,         /* Acknowledge PCH SUSWARN# signal */
	GPIO_PCH_RTCRST_L,         /* Not supposed to be here */
	GPIO_PCH_SRTCRST_L,        /* Not supposed to be here */

	GPIO_BAT_LED0_L,           /* Battery charging LED - Blue */
	GPIO_BAT_LED1_L,           /* Battery charging LED - Amber */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* x86 signal definitions */
enum x86_signal {
	X86_PGOOD_PP5000 = 0,
	X86_PGOOD_PP1350,
	X86_PGOOD_PP1050,
	X86_PGOOD_VCORE,
	X86_PCH_SLP_S0n_DEASSERTED,
	X86_PCH_SLP_S3n_DEASSERTED,
	X86_PCH_SLP_S5n_DEASSERTED,
	X86_PCH_SLP_SUSn_DEASSERTED,

	/* Number of X86 signals */
	X86_SIGNAL_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10 /* Input sensor resistor, mOhm */
#define CONFIG_CHARGER_INPUT_CURRENT 4032 /* mA, about half max */


enum adc_channel {
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,

	/* HEY: Be prepared to read this (ICMNT). */
	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

enum temp_sensor_id {
	/* CPU die temperature via PECI */
	TEMP_SENSOR_CPU_PECI = 0,
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL,
	/* G781 internal and external sensors */
	TEMP_SENSOR_I2C_G781_INTERNAL,
	TEMP_SENSOR_I2C_G781_EXTERNAL,

	TEMP_SENSOR_COUNT
};

/**
 * Board-specific g781 power state.
 */
int board_g781_has_power(void);

/* HEY: The below stuff is for Link. Pick a different pin for Slippy */
/* Target value for BOOTCFG. This is set to PE2/USB1_CTL1, which has an external
 * pullup. If this signal is pulled to ground when the EC boots, the EC will get
 * into the boot loader and we can recover bricked EC. */
#define BOOTCFG_VALUE 0x7fff88fe

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_WLAN_OFF_L
#define WIRELESS_GPIO_WWAN GPIO_PP3300_LTE_EN
#define WIRELESS_GPIO_WLAN_POWER GPIO_PP3300_WLAN_EN

/* Discharge battery when on AC power for factory test. */
int board_discharge_on_ac(int enable);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
