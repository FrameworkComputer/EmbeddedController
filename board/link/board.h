/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Link mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* 66.667 Mhz clock frequency */
#define CPU_CLOCK  66666667

/* Fan PWM channels */
#define FAN_CH_CPU       0  /* CPU fan */
#define FAN_CH_KBLIGHT   1  /* Keyboard backlight */
#define FAN_CH_POWER_LED 5  /* Power adapter LED */

/* TODO: these should really only be used inside lpc.c; once they are, remove
 * from board header files. */
/* LPC channels */
#define LPC_CH_KERNEL   0  /* Kernel commands */
#define LPC_CH_PORT80   1  /* Port 80 debug output */
#define LPC_CH_CMD_DATA 2  /* Data for kernel/user-mode commands */
#define LPC_CH_KEYBOARD 3  /* 8042 keyboard emulation */
#define LPC_CH_USER     4  /* User-mode commands */
#define LPC_CH_COMX     7  /* UART emulation */
/* LPC pool offsets */
#define LPC_POOL_OFFS_KERNEL     0  /* Kernel commands - 0=in, 1=out */
#define LPC_POOL_OFFS_PORT80     4  /* Port 80 - 4=in, 5=out */
#define LPC_POOL_OFFS_COMX       8  /* UART emulation range - 8-15 */
#define LPC_POOL_OFFS_KEYBOARD  16  /* Keyboard - 16=in, 17=out */
#define LPC_POOL_OFFS_USER      20  /* User commands - 20=in, 21=out */
#define LPC_POOL_OFFS_CMD_DATA 512  /* Data range for commands - 512-1023 */
/* LPC pool data pointers */
#define LPC_POOL_KERNEL   (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_KERNEL)
#define LPC_POOL_PORT80   (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_PORT80)
#define LPC_POOL_COMX     (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_COMX)
#define LPC_POOL_KEYBOARD (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_KEYBOARD)
#define LPC_POOL_CMD_DATA (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_CMD_DATA)
#define LPC_POOL_USER     (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_USER)
/* LPC COMx I/O address (in x86 I/O address space) */
#define LPC_COMX_ADDR 0x3f8  /* COM1 */

/* ADC inputs */
/* TODO: assign real ADC inputs */
#define ADC_IN0 11  /* Charger current */

enum adc_channel
{
	/* EC internal die temperature in degrees K. */
	ADC_CH_EC_TEMP = 0,
	/* Charger current in mA. */
	ADC_CH_CHARGER_CURRENT,

	ADC_CH_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_BQ24725

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 1
#define I2C_PORT_THERMAL 5
/* I2C port speeds in kbps */
#define I2C_SPEED_BATTERY 100
#define I2C_SPEED_CHARGER 100
#define I2C_SPEED_THERMAL 400  /* TODO: TMP007 supports 3.4Mbps
				  operation; use faster speed? */

/* Keyboard scanner uses an entire GPIO bank for row inputs */
#define KB_SCAN_ROW_IRQ  LM4_IRQ_GPION
#define KB_SCAN_ROW_GPIO LM4_GPIO_N

/* USB charge port */
#define USB_CHARGE_PORT_COUNT 2

/* GPIO signal definitions. */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_POWER_BUTTONn = 0,   /* Power button */
	GPIO_LID_SWITCHn,         /* Lid switch */
	GPIO_POWER_ONEWIRE,       /* 1-wire interface to power adapter LEDs */
	GPIO_THERMAL_DATA_READYn, /* Data ready from I2C thermal sensor */
	/* Other inputs */
	GPIO_AC_PRESENT,          /* AC power present */
	GPIO_PCH_BKLTEN,          /* Backlight enable signal from PCH */
	GPIO_PCH_SLP_An,          /* SLP_A# signal from PCH */
	GPIO_PCH_SLP_ME_CSW_DEVn, /* SLP_ME_CSW_DEV# signal from PCH */
	GPIO_PCH_SLP_S3n,         /* SLP_S3# signal from PCH */
	GPIO_PCH_SLP_S4n,         /* SLP_S4# signal from PCH */
	GPIO_PCH_SLP_S5n,         /* SLP_S5# signal from PCH */
	GPIO_PCH_SLP_SUSn,        /* SLP_SUS# signal from PCH */
	GPIO_PCH_SUSWARNn,        /* SUSWARN# signal from PCH */
	GPIO_PGOOD_1_5V_DDR,      /* Power good on +1.5V_DDR */
	GPIO_PGOOD_1_5V_PCH,      /* Power good on +1.5V_PCH */
	GPIO_PGOOD_1_8VS,         /* Power good on +1.8VS */
	GPIO_PGOOD_5VALW,         /* Power good on +5VALW */
	GPIO_PGOOD_CPU_CORE,      /* Power good on +CPU_CORE */
	GPIO_PGOOD_VCCP,          /* Power good on +VCCP */
	GPIO_PGOOD_VCCSA,         /* Power good on +VCCSA */
	GPIO_PGOOD_VGFX_CORE,     /* Power good on +VGFX_CORE */
	GPIO_RECOVERYn,           /* Recovery signal from servo */
	GPIO_USB1_STATUSn,        /* USB charger port 1 status output */
	GPIO_USB2_STATUSn,        /* USB charger port 2 status output */
	GPIO_WRITE_PROTECTn,      /* Write protect input */
	/* Outputs */
	GPIO_CPU_PROCHOTn,        /* Force CPU to think it's overheated */
	GPIO_ENABLE_1_5V_DDR,     /* Enable +1.5V_DDR supply */
	GPIO_ENABLE_BACKLIGHT,    /* Enable backlight power */
	GPIO_ENABLE_VCORE,        /* Enable +CPU_CORE and +VGFX_CORE */
	GPIO_ENABLE_VS,           /* Enable VS power supplies */
	GPIO_ENTERING_RW,         /* Indicate when EC is entering RW code */
	GPIO_PCH_A20GATE,         /* A20GATE signal to PCH */
	GPIO_PCH_DPWROK,          /* DPWROK signal to PCH */
	GPIO_PCH_HDA_SDO,         /* HDA_SDO signal to PCH; when high, ME
				   * ignores security descriptor */
	GPIO_PCH_LID_SWITCHn,     /* Lid switch output to PCH */
	GPIO_PCH_NMIn,            /* Non-maskable interrupt pin to PCH */
	GPIO_PCH_PWRBTNn,         /* Power button output to PCH */
	GPIO_PCH_PWROK,           /* PWROK / APWROK signals to PCH */
	GPIO_PCH_RCINn,           /* RCIN# signal to PCH */
	GPIO_PCH_RSMRSTn,         /* Reset PCH resume power plane logic */
	GPIO_PCH_SMIn,            /* System management interrupt to PCH */
	GPIO_PCH_SUSACKn,         /* Acknowledge PCH SUSWARN# signal */
	GPIO_SHUNT_1_5V_DDR,      /* Shunt +1.5V_DDR; may also enable +3V_TP
				   * depending on stuffing. */
	GPIO_USB1_CTL1,           /* USB charger port 1 CTL1 output */
	GPIO_USB1_CTL2,           /* USB charger port 1 CTL2 output */
	GPIO_USB1_CTL3,           /* USB charger port 1 CTL3 output */
	GPIO_USB1_ENABLE,         /* USB charger port 1 enable */
	GPIO_USB1_ILIM_SEL,       /* USB charger port 1 ILIM_SEL output */
	GPIO_USB2_CTL1,           /* USB charger port 2 CTL1 output */
	GPIO_USB2_CTL2,           /* USB charger port 2 CTL2 output */
	GPIO_USB2_CTL3,           /* USB charger port 2 CTL3 output */
	GPIO_USB2_ENABLE,         /* USB charger port 2 enable */
	GPIO_USB2_ILIM_SEL,       /* USB charger port 2 ILIM_SEL output */

	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

enum temp_sensor_id {
	/* I2C die temperature sensor near CPU */
	TEMP_SENSOR_I2C_DIE_NEAR_CPU = 0,
	/* PCH temperature sensor */
	TEMP_SENSOR_I2C_DIE_NEAR_PCH,
	/* DDR memory temperature sensor */
	TEMP_SENSOR_I2C_DIE_NEAR_DDR,
	/* Battery charger temperature sensor */
	TEMP_SENSOR_I2C_DIE_NEAR_CHARGER,
	/* EC internal temperature sensor */
	TEMP_SENSOR_EC_INTERNAL,

	/* TODO: I2C temperature sensors. */

	TEMP_SENSOR_COUNT
};

void configure_board(void);

#endif /* __BOARD_H */
