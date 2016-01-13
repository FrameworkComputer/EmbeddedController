/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "gpio.h"
#include "keyboard_config.h"
#include "hooks.h"
#include "registers.h"
#include "switch.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "system.h"
#include "system_chip.h"
#include "lpc_chip.h"

/* Marco functions for GPIO WUI/ALT table */
#define NPCX_GPIO(grp, pin) \
	GPIO_PORT_##grp, MASK_PIN##pin
#define NPCX_GPIO_NONE \
	GPIO_PORT_COUNT, 0xFF
#define NPCX_WUI(tbl, grp, pin) \
	MIWU_TABLE_##tbl, MIWU_GROUP_##grp, MASK_PIN##pin

#define ALT_MASK(pin) \
	CONCAT2(MASK_PIN, pin)
#define ALT_PIN(grp, pin) \
	ALT_MASK(NPCX_DEVALT##grp##_##pin)
#define NPCX_ALT(grp, pin) \
	ALT_GROUP_##grp, ALT_PIN(grp, pin)

/* Flags for PWM IO type */
#define PWM_IO_FUNC        (1 << 1)  /* PWM optional func bit */
#define PWM_IO_OD          (1 << 2)  /* PWM IO open-drain bit */

struct gpio_wui_map {
	uint8_t gpio_port;
	uint8_t gpio_mask;
	uint8_t wui_table;
	uint8_t wui_group;
	uint8_t wui_mask;
};

struct gpio_wui_item {
	struct  gpio_wui_map wui_map[8];
	uint8_t irq;
};

const struct gpio_wui_item gpio_wui_table[] = {
	/* MIWU0 Group A */
	{ {	{ NPCX_GPIO(8, 0), NPCX_WUI(0, 1, 0) },
		{ NPCX_GPIO(8, 1), NPCX_WUI(0, 1, 1) },
		{ NPCX_GPIO(8, 2), NPCX_WUI(0, 1, 2) },
		{ NPCX_GPIO(8, 3), NPCX_WUI(0, 1, 3) },
		{ NPCX_GPIO(8, 4), NPCX_WUI(0, 1, 4) },
		{ NPCX_GPIO(8, 5), NPCX_WUI(0, 1, 5) },
		{ NPCX_GPIO(8, 6), NPCX_WUI(0, 1, 6) },
		{ NPCX_GPIO(8, 7), NPCX_WUI(0, 1, 7) },
	  },  NPCX_IRQ_MTC_WKINTAD_0 },
	/* MIWU0 Group B */
	{ {	{ NPCX_GPIO(9, 0), NPCX_WUI(0, 2, 0) },
		{ NPCX_GPIO(9, 1), NPCX_WUI(0, 2, 1) },
		{ NPCX_GPIO(9, 2), NPCX_WUI(0, 2, 2) },
		{ NPCX_GPIO(9, 3), NPCX_WUI(0, 2, 3) },
		{ NPCX_GPIO(9, 4), NPCX_WUI(0, 2, 4) },
		{ NPCX_GPIO(9, 5), NPCX_WUI(0, 2, 5) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 2, 6) }, /* MSWC Wake-Up	*/
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 2, 7) }, /* T0OUT Wake-Up */
	  },  NPCX_IRQ_TWD_WKINTB_0 },
	/* MIWU0 Group C */
	{ {	{ NPCX_GPIO(9, 6), NPCX_WUI(0, 3, 0) },
		{ NPCX_GPIO(9, 7), NPCX_WUI(0, 3, 1) },
		{ NPCX_GPIO(A, 0), NPCX_WUI(0, 3, 2) },
		{ NPCX_GPIO(A, 1), NPCX_WUI(0, 3, 3) },
		{ NPCX_GPIO(A, 2), NPCX_WUI(0, 3, 4) },
		{ NPCX_GPIO(A, 3), NPCX_WUI(0, 3, 5) },
		{ NPCX_GPIO(A, 4), NPCX_WUI(0, 3, 6) },
		{ NPCX_GPIO(A, 5), NPCX_WUI(0, 3, 7) },
	  },  NPCX_IRQ_WKINTC_0 },
	/* MIWU0 Group D */
	{ {	{ NPCX_GPIO(A, 6), NPCX_WUI(0, 4, 0) },
		{ NPCX_GPIO(A, 7), NPCX_WUI(0, 4, 1) },
		{ NPCX_GPIO(B, 0), NPCX_WUI(0, 4, 2) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 4, 3) }, /* SMB0 Wake-Up */
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 4, 4) }, /* SMB1 Wake-Up */
		{ NPCX_GPIO(B, 1), NPCX_WUI(0, 4, 5) },
		{ NPCX_GPIO(B, 2), NPCX_WUI(0, 4, 6) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 4, 7) }, /* MTC Wake-Up */
	  },  NPCX_IRQ_MTC_WKINTAD_0 },

	/* MIWU0 Group E */
	{ {	{ NPCX_GPIO(B, 3), NPCX_WUI(0, 5 , 0) },
		{ NPCX_GPIO(B, 4), NPCX_WUI(0, 5 , 1) },
		{ NPCX_GPIO(B, 5), NPCX_WUI(0, 5 , 2) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 5 , 3) },
		{ NPCX_GPIO(B, 7), NPCX_WUI(0, 5 , 4) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 5 , 5) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 5 , 6) }, /* Host Wake-Up */
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 5 , 7) }, /* LRESET Wake-Up */
	  },  NPCX_IRQ_WKINTEFGH_0 },

	/* MIWU0 Group F */
	{ {	{ NPCX_GPIO(C, 0), NPCX_WUI(0, 6, 0) },
		{ NPCX_GPIO(C, 1), NPCX_WUI(0, 6, 1) },
		{ NPCX_GPIO(C, 2), NPCX_WUI(0, 6, 2) },
		{ NPCX_GPIO(C, 3), NPCX_WUI(0, 6, 3) },
		{ NPCX_GPIO(C, 4), NPCX_WUI(0, 6, 4) },
		{ NPCX_GPIO(C, 5), NPCX_WUI(0, 6, 5) },
		{ NPCX_GPIO(C, 6), NPCX_WUI(0, 6, 6) },
		{ NPCX_GPIO(C, 7), NPCX_WUI(0, 6, 7) },
	  },  NPCX_IRQ_WKINTEFGH_0 },
	/* MIWU0 Group G */
	{ {	{ NPCX_GPIO(D, 0), NPCX_WUI(0, 7, 0) },
		{ NPCX_GPIO(D, 1), NPCX_WUI(0, 7, 1) },
		{ NPCX_GPIO(D, 2), NPCX_WUI(0, 7, 2) },
		{ NPCX_GPIO(D, 3), NPCX_WUI(0, 7, 3) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 7, 4) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 7, 5) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 7, 6) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 7, 7) },
	  },  NPCX_IRQ_WKINTEFGH_0 },
	/* MIWU0 Group H */
	{ {	{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 0) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 1) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 2) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 3) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 4) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 5) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(0, 8, 6) },
		{ NPCX_GPIO(E, 7), NPCX_WUI(0, 8, 7) },
	  },  NPCX_IRQ_WKINTEFGH_0 },

	/* MIWU1 Group A */
	{ {	{ NPCX_GPIO(0, 0), NPCX_WUI(1, 1, 0) },
		{ NPCX_GPIO(0, 1), NPCX_WUI(1, 1, 1) },
		{ NPCX_GPIO(0, 2), NPCX_WUI(1, 1, 2) },
		{ NPCX_GPIO(0, 3), NPCX_WUI(1, 1, 3) },
		{ NPCX_GPIO(0, 4), NPCX_WUI(1, 1, 4) },
		{ NPCX_GPIO(0, 5), NPCX_WUI(1, 1, 5) },
		{ NPCX_GPIO(0, 6), NPCX_WUI(1, 1, 6) },
		{ NPCX_GPIO(0, 7), NPCX_WUI(1, 1, 7) },
	  },  NPCX_IRQ_WKINTA_1 },
	/* MIWU1 Group B */
	{ {	{ NPCX_GPIO(1, 0), NPCX_WUI(1, 2, 0) },
		{ NPCX_GPIO(1, 1), NPCX_WUI(1, 2, 1) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(1, 2, 2) },
		{ NPCX_GPIO(1, 3), NPCX_WUI(1, 2, 3) },
		{ NPCX_GPIO(1, 4), NPCX_WUI(1, 2, 4) },
		{ NPCX_GPIO(1, 5), NPCX_WUI(1, 2, 5) },
		{ NPCX_GPIO(1, 6), NPCX_WUI(1, 2, 6) },
		{ NPCX_GPIO(1, 7), NPCX_WUI(1, 2, 7) },
	  },  NPCX_IRQ_WKINTB_1 },
	/* MIWU1 Group C -- Skipping */
	/* MIWU1 Group D */
	{ {	{ NPCX_GPIO(2, 0), NPCX_WUI(1, 4, 0) },
		{ NPCX_GPIO(2, 1), NPCX_WUI(1, 4, 1) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(1, 4, 2) },
		{ NPCX_GPIO(3, 3), NPCX_WUI(1, 4, 3) },
		{ NPCX_GPIO(3, 4), NPCX_WUI(1, 4, 4) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(1, 4, 5) },
		{ NPCX_GPIO(3, 6), NPCX_WUI(1, 4, 6) },
		{ NPCX_GPIO(3, 7), NPCX_WUI(1, 4, 7) },
	  },  NPCX_IRQ_WKINTD_1 },

	/* MIWU1 Group E */
	{ {	{ NPCX_GPIO(4, 0), NPCX_WUI(1, 5, 0) },
		{ NPCX_GPIO(4, 1), NPCX_WUI(1, 5, 1) },
		{ NPCX_GPIO(4, 2), NPCX_WUI(1, 5, 2) },
		{ NPCX_GPIO(4, 3), NPCX_WUI(1, 5, 3) },
		{ NPCX_GPIO(4, 4), NPCX_WUI(1, 5, 4) },
		{ NPCX_GPIO(4, 5), NPCX_WUI(1, 5, 5) },
		{ NPCX_GPIO(4, 6), NPCX_WUI(1, 5, 6) },
		{ NPCX_GPIO(4, 7), NPCX_WUI(1, 5, 7) },
	  },  NPCX_IRQ_WKINTE_1 },

	/* MIWU1 Group F */
	{ {	{ NPCX_GPIO(5, 0), NPCX_WUI(1, 6, 0) },
		{ NPCX_GPIO(5, 1), NPCX_WUI(1, 6, 1) },
		{ NPCX_GPIO(5, 2), NPCX_WUI(1, 6, 2) },
		{ NPCX_GPIO(5, 3), NPCX_WUI(1, 6, 3) },
		{ NPCX_GPIO(5, 4), NPCX_WUI(1, 6, 4) },
		{ NPCX_GPIO(5, 5), NPCX_WUI(1, 6, 5) },
		{ NPCX_GPIO(5, 6), NPCX_WUI(1, 6, 6) },
		{ NPCX_GPIO(5, 7), NPCX_WUI(1, 6, 7) },
	  },  NPCX_IRQ_WKINTF_1 },
	/* MIWU1 Group G */
	{ {	{ NPCX_GPIO(6, 0), NPCX_WUI(1, 7, 0) },
		{ NPCX_GPIO(6, 1), NPCX_WUI(1, 7, 1) },
		{ NPCX_GPIO(6, 2), NPCX_WUI(1, 7, 2) },
		{ NPCX_GPIO(6, 3), NPCX_WUI(1, 7, 3) },
		{ NPCX_GPIO(6, 4), NPCX_WUI(1, 7, 4) },
		{ NPCX_GPIO(6, 5), NPCX_WUI(1, 7, 5) },
		{ NPCX_GPIO(6, 6), NPCX_WUI(1, 7, 6) },
		{ NPCX_GPIO(7, 1), NPCX_WUI(1, 7, 7) },
	  },  NPCX_IRQ_WKINTG_1 },
	/* MIWU1 Group H */
	{ {	{ NPCX_GPIO(7, 0), NPCX_WUI(1, 8, 0) },
		{ NPCX_GPIO(6, 7), NPCX_WUI(1, 8, 1) },
		{ NPCX_GPIO(7, 2), NPCX_WUI(1, 8, 2) },
		{ NPCX_GPIO(7, 3), NPCX_WUI(1, 8, 3) },
		{ NPCX_GPIO(7, 4), NPCX_WUI(1, 8, 4) },
		{ NPCX_GPIO(7, 5), NPCX_WUI(1, 8, 5) },
		{ NPCX_GPIO(7, 6), NPCX_WUI(1, 8, 6) },
		{ NPCX_GPIO_NONE,  NPCX_WUI(1, 8, 7) },
	  },  NPCX_IRQ_WKINTH_1 },
};

struct gpio_alt_map {
	uint8_t gpio_port;
	uint8_t gpio_mask;
	uint8_t alt_group;
	uint8_t alt_mask;
};

const struct gpio_alt_map gpio_alt_table[] = {
	/* I2C Module */
	{ NPCX_GPIO(B, 2),  NPCX_ALT(2, I2C0_1_SL)}, /* SMB0SDA1 */
	{ NPCX_GPIO(B, 3),  NPCX_ALT(2, I2C0_1_SL)}, /* SMB0SCL1 */
	{ NPCX_GPIO(B, 4),  NPCX_ALT(2, I2C0_0_SL)}, /* SMB0SDA0 */
	{ NPCX_GPIO(B, 5),  NPCX_ALT(2, I2C0_0_SL)}, /* SMB0SCL0 */
	{ NPCX_GPIO(8, 7),  NPCX_ALT(2, I2C1_0_SL)}, /* SMB1SDA */
	{ NPCX_GPIO(9, 0),  NPCX_ALT(2, I2C1_0_SL)}, /* SMB1SCL */
	{ NPCX_GPIO(9, 1),  NPCX_ALT(2, I2C2_0_SL)}, /* SMB2SDA */
	{ NPCX_GPIO(9, 2),  NPCX_ALT(2, I2C2_0_SL)}, /* SMB2SCL */
	{ NPCX_GPIO(D, 0),  NPCX_ALT(2, I2C3_0_SL)}, /* SMB3SDA */
	{ NPCX_GPIO(D, 1),  NPCX_ALT(2, I2C3_0_SL)}, /* SMB3SCL */
	/* ADC Module */
	{ NPCX_GPIO(4, 5),  NPCX_ALT(6, ADC0_SL)}, /* ADC0  */
	{ NPCX_GPIO(4, 4),  NPCX_ALT(6, ADC1_SL)}, /* ADC1  */
	{ NPCX_GPIO(4, 3),  NPCX_ALT(6, ADC2_SL)}, /* ADC2  */
	{ NPCX_GPIO(4, 2),  NPCX_ALT(6, ADC3_SL)}, /* ADC3  */
	{ NPCX_GPIO(4, 1),  NPCX_ALT(6, ADC4_SL)}, /* ADC4  */
	/* UART Module 1/2 */
#if NPCX_UART_MODULE2
	{ NPCX_GPIO(6, 4),  NPCX_ALT(C, UART_SL2)}, /* CR_SIN */
	{ NPCX_GPIO(6, 5),  NPCX_ALT(C, UART_SL2)}, /* CR_SOUT */
#else
	{ NPCX_GPIO(1, 0),  NPCX_ALT(9, NO_KSO08_SL)}, /* CR_SIN/KSO09 */
	{ NPCX_GPIO(1, 1),  NPCX_ALT(9, NO_KSO09_SL)}, /* CR_SOUT/KSO10 */
#endif
	/* SPI Module */
	{ NPCX_GPIO(9, 5),  NPCX_ALT(0, SPIP_SL)}, /* SPIP_MISO */
	{ NPCX_GPIO(A, 5),  NPCX_ALT(0, SPIP_SL)}, /* SPIP_CS1  */
	{ NPCX_GPIO(A, 3),  NPCX_ALT(0, SPIP_SL)}, /* SPIP_MOSI */
	{ NPCX_GPIO(A, 1),  NPCX_ALT(0, SPIP_SL)}, /* SPIP_SCLK */
	/* PWM Module */
	{ NPCX_GPIO(C, 3),  NPCX_ALT(4, PWM0_SL)}, /* PWM0 */
	{ NPCX_GPIO(C, 2),  NPCX_ALT(4, PWM1_SL)}, /* PWM1 */
	{ NPCX_GPIO(C, 4),  NPCX_ALT(4, PWM2_SL)}, /* PWM2 */
	{ NPCX_GPIO(8, 0),  NPCX_ALT(4, PWM3_SL)}, /* PWM3 */
	{ NPCX_GPIO(B, 6),  NPCX_ALT(4, PWM4_SL)}, /* PWM4 */
	{ NPCX_GPIO(B, 7),  NPCX_ALT(4, PWM5_SL)}, /* PWM5 */
	{ NPCX_GPIO(C, 0),  NPCX_ALT(4, PWM6_SL)}, /* PWM6 */
	{ NPCX_GPIO(6, 0),  NPCX_ALT(4, PWM7_SL)}, /* PWM7 */
	/* MFT Module */
#if NPCX_TACH_SEL2
	{ NPCX_GPIO(9, 3),  NPCX_ALT(C, TA1_TACH1_SL2)},/* TA1_TACH1 */
	{ NPCX_GPIO(D, 3),  NPCX_ALT(C, TB1_TACH2_SL2)},/* TB1_TACH2 */
#else
	{ NPCX_GPIO(4, 0),  NPCX_ALT(3, TA1_TACH1_SL1)},/* TA1_TACH1 */
	{ NPCX_GPIO(A, 4),  NPCX_ALT(3, TB1_TACH2_SL1)},/* TB1_TACH2 */
#endif
};

/*****************************************************************************/
/* Internal functions */
const struct gpio_wui_map *gpio_find_wui_from_io(uint8_t port, uint8_t mask)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(gpio_wui_table); i++) {
		const struct gpio_wui_map *map = gpio_wui_table[i].wui_map;
		for (j = 0; j < 8; j++, map++) {
			if (map->gpio_port == port && map->gpio_mask == mask)
				return map;
		}
	}
	return NULL;
}

int gpio_find_irq_from_io(uint8_t port, uint8_t mask)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(gpio_wui_table); i++) {
		const struct gpio_wui_map *map = gpio_wui_table[i].wui_map;
		for (j = 0; j < 8; j++, map++) {
			if (map->gpio_port == port && map->gpio_mask == mask)
				return gpio_wui_table[i].irq;
		}
	}
	return -1;
}

void gpio_pwm_io_type_sel(uint8_t alt_mask, uint8_t func)
{
	uint8_t chan = 0;
	do {
		alt_mask = (alt_mask >> 1);
		if (alt_mask == 0)
			break;
		chan++;
	} while (1);

	/* Set PWM open drain output is open drain type*/
	if (func & PWM_IO_OD)
		SET_BIT(NPCX_PWMCTLEX(chan), NPCX_PWMCTLEX_OD_OUT);
	else /* Set PWM open drain output is push-pull type*/
		CLEAR_BIT(NPCX_PWMCTLEX(chan), NPCX_PWMCTLEX_OD_OUT);
}

int gpio_alt_sel(uint8_t port, uint8_t mask, int8_t func)
{
	int i;
	const struct gpio_alt_map *map = gpio_alt_table;
	for (i = 0; i < ARRAY_SIZE(gpio_alt_table); i++, map++) {
		if (map->gpio_port == port &&
			(map->gpio_mask == mask)) {
			/* Enable alternative function if func >=0 */
			if (func <= 0) /* GPIO functionality */
				NPCX_DEVALT(map->alt_group) &= ~(map->alt_mask);
			else {
				NPCX_DEVALT(map->alt_group) |= (map->alt_mask);
				/* PWM optional functionality */
				if (func & PWM_IO_FUNC)
					gpio_pwm_io_type_sel(map->alt_mask,
							func);
			}
			return 1;
		}
	}
	return -1;
}

void gpio_execute_isr(uint8_t port, uint8_t mask)
{
	int i;
	const struct gpio_info *g = gpio_list;
	/* Find GPIOs and execute interrupt service routine */
	for (i = 0; i < GPIO_IH_COUNT; i++, g++) {
		if (port == g->port && mask == g->mask) {
			gpio_irq_handlers[i](i);
			return;
		}
	}
}

/* Set interrupt type for GPIO input */
void gpio_interrupt_type_sel(uint8_t port, uint8_t mask, uint32_t flags)
{
	const struct gpio_wui_map *map = gpio_find_wui_from_io(port, mask);
	uint8_t table, group, pmask;

	if (map == NULL)
		return;

	table = map->wui_table;
	group = map->wui_group;
	pmask = map->wui_mask;

	/* Handle interrupt for level trigger */
	if ((flags & GPIO_INT_F_HIGH) ||
			(flags & GPIO_INT_F_LOW)) {
		/* Set detection mode to level */
		NPCX_WKMOD(table, group) |= pmask;
		/* Handle interrupting on level high */
		if (flags & GPIO_INT_F_HIGH)
			NPCX_WKEDG(table, group) &= ~pmask;
		/* Handle interrupting on level low */
		else if (flags & GPIO_INT_F_LOW)
			NPCX_WKEDG(table, group) |= pmask;

		/* Enable wake-up input sources */
		NPCX_WKEN(table, group) |= pmask;
		NPCX_WKINEN(table, group) |= pmask;
		/*
		 * Clear pending bit since it might be set
		 * if WKINEN bit is changed.
		 */
		NPCX_WKPCL(table, group) |= pmask;
	}
	/* Handle interrupt for edge trigger */
	else if ((flags & GPIO_INT_F_RISING) ||
			(flags & GPIO_INT_F_FALLING)) {
		/* Set detection mode to edge */
		NPCX_WKMOD(table, group) &= ~pmask;
		/* Handle interrupting on both edges */
		if ((flags & GPIO_INT_F_RISING) &&
			(flags & GPIO_INT_F_FALLING)) {
			/* Enable any edge */
			NPCX_WKAEDG(table, group) |= pmask;
		}
		/* Handle interrupting on rising edge */
		else if (flags & GPIO_INT_F_RISING) {
			/* Disable any edge */
			NPCX_WKAEDG(table, group) &= ~pmask;
			NPCX_WKEDG(table, group) &= ~pmask;
		}
		/* Handle interrupting on falling edge */
		else if (flags & GPIO_INT_F_FALLING) {
			/* Disable any edge */
			NPCX_WKAEDG(table, group) &= ~pmask;
			NPCX_WKEDG(table, group) |= pmask;
		}

		/* Enable wake-up input sources */
		NPCX_WKEN(table, group) |= pmask;
		NPCX_WKINEN(table, group) |= pmask;
		/*
		 * Clear pending bit since it might be set
		 * if WKINEN bit is changed.
		 */
		NPCX_WKPCL(table, group) |= pmask;
	} else
		/* Disable wake-up input sources */
		NPCX_WKEN(table, group) &= ~pmask;

	/* No support analog mode */
}

/*****************************************************************************/
/* IC specific low-level driver */

void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func)
{
	/* Enable alternative pins by func*/
	int pin;
	uint8_t pmask;
	/* check each bit from mask  */
	for (pin = 0; pin < 8; pin++) {
		pmask = (mask & (1 << pin));
		if (pmask)
			gpio_alt_sel(port, pmask, func);
	}
}

test_mockable int gpio_get_level(enum gpio_signal signal)
{
	return (NPCX_PDIN(gpio_list[signal].port) &
			gpio_list[signal].mask) ? 1 : 0;
}

void gpio_set_level(enum gpio_signal signal, int value)
{
	if (value)
		NPCX_PDOUT(gpio_list[signal].port) |=
				 gpio_list[signal].mask;
	else
		NPCX_PDOUT(gpio_list[signal].port) &=
				~gpio_list[signal].mask;
}

void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags)
{
	/*
	 * Select open drain first, so that we don't glitch the signal
	 * when changing the line to an output. 0:push-pull 1:open-drain
	 */
	if (flags & GPIO_OPEN_DRAIN)
		NPCX_PTYPE(port) |= mask;
	else
		NPCX_PTYPE(port) &= ~mask;

	/* Select direction of GPIO 0:input 1:output */
	if (flags & GPIO_OUTPUT)
		NPCX_PDIR(port) |= mask;
	else
		NPCX_PDIR(port) &= ~mask;

	/* Select pull-up/down of GPIO 0:pull-up 1:pull-down */
	if (flags & GPIO_PULL_UP) {
		NPCX_PPUD(port)  &= ~mask;
		NPCX_PPULL(port) |= mask; /* enable pull down/up */
	} else if (flags & GPIO_PULL_DOWN) {
		NPCX_PPUD(port)  |= mask;
		NPCX_PPULL(port) |= mask; /* enable pull down/up */
	} else {
		/* No pull up/down */
		NPCX_PPULL(port) &= ~mask; /* disable pull down/up */
	}

	/* Set up interrupt type */
	if (flags & GPIO_INPUT)
		gpio_interrupt_type_sel(port, mask, flags);

	/* Set level 0:low 1:high*/
	if (flags & GPIO_HIGH)
		NPCX_PDOUT(port) |= mask;
	else if (flags & GPIO_LOW)
		NPCX_PDOUT(port) &= ~mask;

}

int gpio_enable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	int irq = gpio_find_irq_from_io(g->port, g->mask);

	/* Fail if no interrupt handler */
	if (irq < 0)
		return EC_ERROR_UNKNOWN;

	task_enable_irq(irq);
	return EC_SUCCESS;
}

int gpio_disable_interrupt(enum gpio_signal signal)
{
	const struct gpio_info *g = gpio_list + signal;
	int irq = gpio_find_irq_from_io(g->port, g->mask);

	/* Fail if no interrupt handler */
	if (irq < 0)
		return EC_ERROR_UNKNOWN;

	task_disable_irq(irq);
	return EC_SUCCESS;
}

void gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int is_warm = system_is_reboot_warm();
	int flags;
	int i, j;

	uint32_t ksi_mask = (~((1<<KEYBOARD_ROWS)-1)) & KB_ROW_MASK;
	uint32_t kso_mask = ((~((1<<KEYBOARD_COLS)-1))
			<< CONFIG_KEYBOARD_KSO_BASE) & KB_COL_MASK;

#ifdef CONFIG_KEYBOARD_COL2_INVERTED
	kso_mask |= 1 << (CONFIG_KEYBOARD_KSO_BASE + 2);
#endif

	/* Set necessary pin mux to GPIO first */
	/* Pin_Mux for KSO0-17 & KSI0-7 */
	NPCX_DEVALT(ALT_GROUP_7)  = (uint8_t)(ksi_mask);
	NPCX_DEVALT(ALT_GROUP_8)  = (uint8_t)(kso_mask);
	NPCX_DEVALT(ALT_GROUP_9)  = (uint8_t)(kso_mask >> 8);
	NPCX_DEVALT(ALT_GROUP_A) |= (uint8_t)(kso_mask >> 16);

	/* Pin_Mux for FIU/SPI (set to GPIO) */
	SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_GPIO_NO_SPIP);
	SET_BIT(NPCX_DEVALT(0), NPCX_DEVALT0_NO_F_SPI);

	/* Pin_Mux for PWRGD */
	SET_BIT(NPCX_DEVALT(1), NPCX_DEVALT1_NO_PWRGD);

	/* Pin_Mux for PECI */
#ifndef CONFIG_PECI
	SET_BIT(NPCX_DEVALT(0xA), NPCX_DEVALTA_NO_PECI_EN);
#endif

	/* Pin_Mux for LPC & SHI */
#ifdef CONFIG_SHI
	/* Switching to eSPI mode for SHI interface */
	NPCX_DEVCNT |= 0x08;
	/* Alternate Intel bus interface LPC/eSPI to GPIOs first */
	SET_BIT(NPCX_DEVALT(ALT_GROUP_1), NPCX_DEVALT1_NO_LPC_ESPI);
#endif

	/* Clear all pending bits of GPIOS*/
	for (i = 0; i < 2; i++)
		for (j = 0; j < 8; j++)
			NPCX_WKPCL(i, j) = 0xFF;

	/* No support enable clock for the GPIO port in run and sleep. */
	/* Set flag for each GPIO pin in gpio_list */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		flags = g->flags;

		if (flags & GPIO_DEFAULT)
			continue;
		/*
		 * If this is a warm reboot, don't set the output levels or
		 * we'll shut off the AP.
		 */
		if (is_warm)
			flags &= ~(GPIO_LOW | GPIO_HIGH);

		/* Set up GPIO based on flags */
		gpio_set_flags_by_mask(g->port, g->mask, flags);
	}
}

/* List of GPIO IRQs to enable. Don't automatically enable interrupts for
 * the keyboard input GPIO bank - that's handled separately. Of course the
 * bank is different for different systems. */
static void gpio_init(void)
{
	int i;
	/* Enable IRQs now that pins are set up */
	for (i = 0; i < ARRAY_SIZE(gpio_wui_table); i++)
		task_enable_irq(gpio_wui_table[i].irq);

}
DECLARE_HOOK(HOOK_INIT, gpio_init, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Interrupt handlers */

/**
 * Handle a GPIO interrupt.
 *
 * @param int_no	Interrupt number for GPIO
 */

static void gpio_interrupt(int int_no)
{
#if DEBUG_GPIO
	static uint8_t i, pin, wui_mask;
#else
	uint8_t i, pin, wui_mask;
#endif
	for (i = 0; i < ARRAY_SIZE(gpio_wui_table); i++) {
		/* If interrupt number is the same */
		if (gpio_wui_table[i].irq == int_no) {
			/* Mapping relationship between WUI and GPIO */
			const struct gpio_wui_map *map =
					gpio_wui_table[i].wui_map;
			/* Get pending mask */
			wui_mask = NPCX_WKPND(map->wui_table , map->wui_group);

			/* If pending bits is not zero */
			if (wui_mask) {
				/* Clear pending bits of WUI */
				NPCX_WKPCL(map->wui_table , map->wui_group)
						= wui_mask;

				for (pin = 0; pin < 8; pin++, map++) {
					/* If pending bit is high, execute ISR*/
					if (wui_mask & (1<<pin))
						gpio_execute_isr(map->gpio_port,
								map->gpio_mask);
				}
			}
		}
	}
}

/**
 * Handlers for each GPIO port.  These read and clear the interrupt bits for
 * the port, then call the master handler above.
 */

#define GPIO_IRQ_FUNC(_irq_func, int_no)	\
void _irq_func(void)				\
{						\
	gpio_interrupt(int_no);			\
}

/* If we need to handle the other type interrupts except GPIO, add code here */
void __gpio_wk0efgh_interrupt(void)
{
#ifdef CONFIG_LPC
	if (IS_BIT_SET(NPCX_WKPND(MIWU_TABLE_0 , MIWU_GROUP_5),7))
		lpc_lreset_pltrst_handler();
	else
#endif
		gpio_interrupt(NPCX_IRQ_WKINTEFGH_0);
}

GPIO_IRQ_FUNC(__gpio_wk0ad_interrupt  , NPCX_IRQ_MTC_WKINTAD_0);
GPIO_IRQ_FUNC(__gpio_wk0b_interrupt   , NPCX_IRQ_TWD_WKINTB_0);
GPIO_IRQ_FUNC(__gpio_wk0c_interrupt   , NPCX_IRQ_WKINTC_0);
GPIO_IRQ_FUNC(__gpio_wk1a_interrupt   , NPCX_IRQ_WKINTA_1);
GPIO_IRQ_FUNC(__gpio_wk1b_interrupt   , NPCX_IRQ_WKINTB_1);
GPIO_IRQ_FUNC(__gpio_wk1d_interrupt   , NPCX_IRQ_WKINTD_1);
GPIO_IRQ_FUNC(__gpio_wk1e_interrupt   , NPCX_IRQ_WKINTE_1);
GPIO_IRQ_FUNC(__gpio_wk1f_interrupt   , NPCX_IRQ_WKINTF_1);
GPIO_IRQ_FUNC(__gpio_wk1g_interrupt   , NPCX_IRQ_WKINTG_1);
GPIO_IRQ_FUNC(__gpio_wk1h_interrupt   , NPCX_IRQ_WKINTH_1);

DECLARE_IRQ(NPCX_IRQ_MTC_WKINTAD_0, __gpio_wk0ad_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_TWD_WKINTB_0,  __gpio_wk0b_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTC_0,      __gpio_wk0c_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTEFGH_0,   __gpio_wk0efgh_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTA_1,      __gpio_wk1a_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTB_1,      __gpio_wk1b_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTD_1,      __gpio_wk1d_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTE_1,      __gpio_wk1e_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTF_1,      __gpio_wk1f_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTG_1,      __gpio_wk1g_interrupt, 1);
DECLARE_IRQ(NPCX_IRQ_WKINTH_1,      __gpio_wk1h_interrupt, 1);


#undef GPIO_IRQ_FUNC
