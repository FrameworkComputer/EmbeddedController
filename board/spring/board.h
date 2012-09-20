/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Spring board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000

/* Use USART1 as console serial port */
#define CONFIG_CONSOLE_UART 1

/* use I2C for host communication */
#define CONFIG_I2C

#define CONFIG_HOST_COMMAND_STATUS

/* Debug features */
#define CONFIG_PANIC_HELP
#define CONFIG_ASSERT_HELP
#define CONFIG_CONSOLE_CMDHELP

#undef  CONFIG_TASK_PROFILING
#define CONFIG_WATCHDOG_HELP

/* compute RW firmware hash at startup */
#define CONFIG_VBOOT

/* DE-ACTIVATED: use STOP mode when we have nothing to do */
#undef CONFIG_LOW_POWER_IDLE

/* always enable LCD power rails */
#define CONFIG_PMU_FORCE_FET

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL & ~CC_MASK(CC_KEYSCAN))

/* EC drives 13 outputs to keyboard matrix */
#define KB_OUTPUTS 13

/* Charging */
#define CONFIG_SMART_BATTERY
#define CONFIG_PMU_TPS65090
#define CONFIG_PMU_BOARD_INIT
#define I2C_PORT_HOST 0
#define I2C_PORT_BATTERY I2C_PORT_HOST
#define I2C_PORT_CHARGER I2C_PORT_HOST
#define I2C_PORT_SLAVE 1

#define CONFIG_CMD_PMU

/* Battery */
#define CONFIG_BATTERY_BQ20Z453

/* ADC signal */
#define CONFIG_ADC
enum adc_channel {
	ADC_CH_USB_ID = 0,
	ADC_CH_USB_DP_SNS,
	ADC_CH_USB_DN_SNS,

	ADC_CH_COUNT
};

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_KB_PWR_ON_L = 0,  /* Keyboard power button */
	GPIO_PP1800_LDO2,      /* LDO2 is ON (end of PMIC sequence) */
	GPIO_SOC1V8_XPSHOLD,   /* App Processor ON  */
	GPIO_CHARGER_INT,
	GPIO_LID_OPEN,         /* LID switch detection */
	GPIO_SUSPEND_L,        /* AP suspend/resume state */
	GPIO_WRITE_PROTECTn,   /* Write protection pin (low active) */
	/* Keyboard inputs */
	GPIO_KB_IN00,
	GPIO_KB_IN01,
	GPIO_KB_IN02,
	GPIO_KB_IN03,
	GPIO_KB_IN04,
	GPIO_KB_IN05,
	GPIO_KB_IN06,
	GPIO_KB_IN07,
	GPIO_USB_CHG_INT,
	/* Other inputs */
	GPIO_BCHGR_VACG,       /* AC good on TPSChrome */
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
	GPIO_I2C2_SCL,
	GPIO_I2C2_SDA,
	/* Outputs */
	GPIO_EN_PP1350,        /* DDR 1.35v rail enable */
	GPIO_EN_PP5000,        /* 5.0v rail enable */
	GPIO_EN_PP3300,        /* 3.3v rail enable */
	GPIO_PMIC_PWRON_L,     /* 5v rail ready */
	GPIO_PMIC_RESET,       /* Force hard reset of the pmic */
	GPIO_ENTERING_RW,      /* EC is R/W mode for the kbc mux */
	GPIO_CHARGER_EN,
	GPIO_EC_INT,
	GPIO_CODEC_INT,        /* To audio codec (KB noise cancellation) */
	GPIO_KB_OUT00,
	GPIO_KB_OUT01,
	GPIO_KB_OUT02,
	GPIO_KB_OUT03,
	GPIO_KB_OUT04,
	GPIO_KB_OUT05,
	GPIO_KB_OUT06,
	GPIO_KB_OUT07,
	GPIO_KB_OUT08,
	GPIO_KB_OUT09,
	GPIO_KB_OUT10,
	GPIO_KB_OUT11,
	GPIO_KB_OUT12,
	GPIO_ILIM_1500,        /* max charging current : 1500 mA */
	GPIO_ILIM_500,         /* max charging current :  500 mA */
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

void configure_board(void);

void matrix_interrupt(enum gpio_signal signal);

/* Signal to AP that data is waiting */
void board_interrupt_host(int active);

/* Initialize PMU registers using board settings */
int board_pmu_init(void);

/* Force the pmu to reset everything on the board */
void board_hard_reset(void);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
