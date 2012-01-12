/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "common.h"

/* GPIO signal definitions. */
/* TODO: the exact list is board-depdendent */
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
	GPIO_DEBUG_LED,           /* Debug LED */
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


/* Pre-initializes the module.  This occurs before clocks or tasks are
 * set up. */
int gpio_pre_init(void);

/* Initializes the GPIO module. */
int gpio_init(void);

/* Functions should return an error if the requested signal is not
 * supported / not present on the board. */

/* Gets the current value of a signal (0=low, 1=hi). */
int gpio_get_level(enum gpio_signal signal);

/* Sets the current value of a signal.  Returns error if the signal is
 * not supported or is an input signal. */
int gpio_set_level(enum gpio_signal signal, int value);

#endif  /* __CROS_EC_GPIO_H */
