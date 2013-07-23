/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Link mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_BACKLIGHT_X86
#define CONFIG_BATTERY_LINK
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_CHARGER
#define CONFIG_CHARGER_BQ24725
#ifdef HAS_TASK_CHIPSET
#define CONFIG_CHIPSET_IVYBRIDGE
#endif
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_KEYBOARD_BOARD_CONFIG
#ifdef HAS_TASK_KEYPROTO
#define CONFIG_KEYBOARD_PROTOCOL_8042
#endif
#define CONFIG_LID_SWITCH
#define CONFIG_LPC
#define CONFIG_ONEWIRE
#define  ONEWIRE_BANK LM4_GPIO_H
#define  ONEWIRE_PIN (1 << 2)
#define CONFIG_ONEWIRE_LED
#define CONFIG_PECI
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_PWM_FAN
#define CONFIG_PWM_KBLIGHT
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP006
#define CONFIG_USB_PORT_POWER_SMART
#define CONFIG_WIRELESS
#define CONFIG_WP_ACTIVE_HIGH

#ifndef __ASSEMBLER__

/* Fan PWM channels */
#define FAN_CH_CPU       0  /* CPU fan */
#define FAN_CH_KBLIGHT   1  /* Keyboard backlight */
#define FAN_CH_POWER_LED 5  /* Power adapter LED */

enum adc_channel
{
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,
	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* Input sensor resistor, mOhm */
#define CONFIG_CHARGER_INPUT_CURRENT 4032 /* mA, based on Link HW design */
#define CONFIG_CHARGER_CURRENT_LIMIT 3000 /* PL102 inductor 3.0A(3.8A) */

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 0  /* Note: proto0 used port 1 */
#define I2C_PORT_THERMAL 5
#define I2C_PORT_LIGHTBAR 1
#define I2C_PORT_REGULATOR 0
/* There are only 3 I2C ports used because battery and charger share a port */
#define I2C_PORTS_USED 3

/* 13x8 keyboard scanner uses an entire GPIO bank for row inputs */
#define KB_SCAN_ROW_IRQ  LM4_IRQ_GPION
#define KB_SCAN_ROW_GPIO LM4_GPIO_N

/* Host connects to keyboard controller module via LPC */
#define HOST_KB_BUS_LPC

/* USB charge port */
#define USB_CHARGE_PORT_COUNT 2

/* GPIOs for second UART port */
#define CONFIG_HOST_UART 1
#define CONFIG_HOST_UART_IRQ LM4_IRQ_UART1
#define CONFIG_HOST_UART1_GPIOS_PC4_5

/* GPIO signal definitions. */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_POWER_BUTTON_L = 0,   /* Power button */
	GPIO_LID_OPEN,             /* Lid switch */
	GPIO_THERMAL_DATA_READY_L, /* Data ready from I2C thermal sensor */
	/* Other inputs */
	GPIO_AC_PRESENT,           /* AC power present */
	GPIO_BOARD_VERSION1,       /* Board version stuffing resistor 1 */
	GPIO_BOARD_VERSION2,       /* Board version stuffing resistor 2 */
	GPIO_BOARD_VERSION3,       /* Board version stuffing resistor 3 */
	GPIO_PCH_BKLTEN,           /* Backlight enable signal from PCH */
	GPIO_PCH_SLP_A_L,          /* SLP_A# signal from PCH */
	GPIO_PCH_SLP_ME_CSW_DEV_L, /* SLP_ME_CSW_DEV# signal from PCH */
	GPIO_PCH_SLP_S3_L,         /* SLP_S3# signal from PCH */
	GPIO_PCH_SLP_S4_L,         /* SLP_S4# signal from PCH */
	GPIO_PCH_SLP_S5_L,         /* SLP_S5# signal from PCH */
	GPIO_PCH_SLP_SUS_L,        /* SLP_SUS# signal from PCH */
	GPIO_PCH_SUSWARN_L,        /* SUSWARN# signal from PCH */
	GPIO_PGOOD_1_5V_DDR,       /* Power good on +1.5V_DDR */
	GPIO_PGOOD_1_5V_PCH,       /* Power good on +1.5V_PCH */
	GPIO_PGOOD_1_8VS,          /* Power good on +1.8VS */
	GPIO_PGOOD_5VALW,          /* Power good on +5VALW */
	GPIO_PGOOD_CPU_CORE,       /* Power good on +CPU_CORE */
	GPIO_PGOOD_VCCP,           /* Power good on +VCCP */
	GPIO_PGOOD_VCCSA,          /* Power good on +VCCSA */
	GPIO_PGOOD_VGFX_CORE,      /* Power good on +VGFX_CORE */
	GPIO_RECOVERY_L,           /* Recovery signal from servo */
	GPIO_USB1_STATUS_L,        /* USB charger port 1 status output */
	GPIO_USB2_STATUS_L,        /* USB charger port 2 status output */
	GPIO_WP,                   /* Write protect input */
	/* Outputs */
	GPIO_CPU_PROCHOT,          /* Force CPU to think it's overheated */
	GPIO_ENABLE_1_5V_DDR,      /* Enable +1.5V_DDR supply */
	GPIO_ENABLE_5VALW,         /* Enable +5V always on rail */
	GPIO_ENABLE_BACKLIGHT,     /* Enable backlight power */
	GPIO_ENABLE_TOUCHPAD,      /* Enable touchpad power */
	GPIO_ENABLE_VCORE,         /* Enable +CPU_CORE and +VGFX_CORE */
	GPIO_ENABLE_VS,            /* Enable VS power supplies */
	GPIO_ENABLE_WLAN,          /* Enable WLAN module power (+3VS_WLAN) */
	GPIO_ENTERING_RW,          /* Indicate when EC is entering RW code */
	GPIO_LIGHTBAR_RESET_L,     /* Reset lightbar controllers */
	GPIO_PCH_A20GATE,          /* A20GATE signal to PCH */
	GPIO_PCH_DPWROK,           /* DPWROK signal to PCH */
	GPIO_PCH_HDA_SDO,          /* HDA_SDO signal to PCH; when high, ME
				    * ignores security descriptor */
	GPIO_PCH_WAKE_L,           /* Wake signal output to PCH */
	GPIO_PCH_NMI_L,            /* Non-maskable interrupt pin to PCH */
	GPIO_PCH_PWRBTN_L,         /* Power button output to PCH */
	GPIO_PCH_PWROK,            /* PWROK / APWROK signals to PCH */
	GPIO_PCH_RCIN_L,           /* RCIN# signal to PCH */
	GPIO_PCH_RSMRST_L,         /* Reset PCH resume power plane logic */
	GPIO_PCH_RTCRST_L,         /* Reset PCH RTC well */
	GPIO_PCH_SMI_L,            /* System management interrupt to PCH */
	GPIO_PCH_SRTCRST_L,        /* Reset PCH ME RTC well */
	GPIO_PCH_SUSACK_L,         /* Acknowledge PCH SUSWARN# signal */
	GPIO_RADIO_ENABLE_WLAN,    /* Enable WLAN radio */
	GPIO_RADIO_ENABLE_BT,      /* Enable bluetooth radio */
	GPIO_SPI_CS_L,             /* SPI chip select */
	GPIO_TOUCHSCREEN_RESET_L,  /* Reset touch screen */
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
	X86_PGOOD_5VALW = 0,
	X86_PGOOD_1_5V_DDR,
	X86_PGOOD_1_5V_PCH,
	X86_PGOOD_1_8VS,
	X86_PGOOD_VCCP,
	X86_PGOOD_VCCSA,
	X86_PGOOD_CPU_CORE,
	X86_PGOOD_VGFX_CORE,
	X86_PCH_SLP_S3n_DEASSERTED,
	X86_PCH_SLP_S4n_DEASSERTED,
	X86_PCH_SLP_S5n_DEASSERTED,
	X86_PCH_SLP_An_DEASSERTED,
	X86_PCH_SLP_SUSn_DEASSERTED,
	X86_PCH_SLP_MEn_DEASSERTED,

	/* Number of X86 signals */
	X86_SIGNAL_COUNT
};

enum temp_sensor_id {
	/* TMP006 U20, die/object temperature near Mini-DP / USB connectors */
	TEMP_SENSOR_I2C_U20_DIE = 0,
	TEMP_SENSOR_I2C_U20_OBJECT,
	/* TMP006 U11, die/object temperature near PCH */
	TEMP_SENSOR_I2C_U11_DIE,
	TEMP_SENSOR_I2C_U11_OBJECT,
	/* TMP006 U27, die/object temperature near hinge */
	TEMP_SENSOR_I2C_U27_DIE,
	TEMP_SENSOR_I2C_U27_OBJECT,
	/* TMP006 U14, die/object temperature near battery charger */
	TEMP_SENSOR_I2C_U14_DIE,
	TEMP_SENSOR_I2C_U14_OBJECT,
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL,
	/* CPU die temperature via PECI */
	TEMP_SENSOR_CPU_PECI,

	TEMP_SENSOR_COUNT
};

/* The number of TMP006 sensor chips on the board. */
#define TMP006_COUNT 4

/* Target value for BOOTCFG. This is set to PE2/USB1_CTL1, which has an external
 * pullup. If this signal is pulled to ground when the EC boots, the EC will get
 * into the boot loader and we can recover bricked EC. */
#define BOOTCFG_VALUE 0x7fff88fe

/* Wireless signals */
#define WIRELESS_GPIO_WLAN GPIO_RADIO_ENABLE_WLAN
#define WIRELESS_GPIO_BLUETOOTH GPIO_RADIO_ENABLE_BT
#define WIRELESS_GPIO_WLAN_POWER GPIO_ENABLE_WLAN

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
