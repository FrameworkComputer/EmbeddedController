/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "console.h"
#include "gpio.h"
#include "power_button.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"


/* 0-terminated list of GPIO bases */
const uint32_t gpio_bases[] = {
	LM4_GPIO_A, LM4_GPIO_B, LM4_GPIO_C, LM4_GPIO_D,
	LM4_GPIO_E, LM4_GPIO_F, LM4_GPIO_G, LM4_GPIO_H,
	LM4_GPIO_J, LM4_GPIO_K, LM4_GPIO_L, LM4_GPIO_M,
	LM4_GPIO_N, LM4_GPIO_P, LM4_GPIO_Q, 0
};


struct gpio_info {
	const char *name;
	int port;   /* Port (LM4_GPIO_*) */
	int mask;   /* Bitmask on that port (0x01 - 0x80; 0x00 =
		       signal not implemented) */
	void (*irq_handler)(enum gpio_signal signal);
};

/* Macro for signals which don't exist */
#define SIGNAL_NOT_IMPLEMENTED(name) {name, LM4_GPIO_A, 0x00, NULL}

/* Signal information.  Must match order from enum gpio_signal. */
const struct gpio_info signal_info[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTONn", LM4_GPIO_C, 0x20, power_button_interrupt},
	{"LID_SWITCHn",   LM4_GPIO_D, 0x01, power_button_interrupt},
	SIGNAL_NOT_IMPLEMENTED("POWER_ONEWIRE"),
	SIGNAL_NOT_IMPLEMENTED("THERMAL_DATA_READYn"),
	/* Other inputs */
	SIGNAL_NOT_IMPLEMENTED("AC_PRESENT"),
	SIGNAL_NOT_IMPLEMENTED("PCH_BKLTEN"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SLP_An"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SLP_ME_CSW_DEVn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SLP_S3n"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SLP_S4n"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SLP_S5n"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SLP_SUSn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SUSWARNn"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_1_5V_DDR"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_1_5V_PCH"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_1_8VS"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_5VALW"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_CPU_CORE"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_VCCP"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_VCCSA"),
	SIGNAL_NOT_IMPLEMENTED("PGOOD_VGFX_CORE"),
	SIGNAL_NOT_IMPLEMENTED("RECOVERYn"),
	SIGNAL_NOT_IMPLEMENTED("USB1_STATUSn"),
	SIGNAL_NOT_IMPLEMENTED("USB2_STATUSn"),
	SIGNAL_NOT_IMPLEMENTED("WRITE_PROTECTn"),
	/* Outputs */
	SIGNAL_NOT_IMPLEMENTED("CPU_PROCHOTn"),
	{"DEBUG_LED",    LM4_GPIO_A, 0x80, NULL},
	SIGNAL_NOT_IMPLEMENTED("ENABLE_1_5V_DDR"),
	SIGNAL_NOT_IMPLEMENTED("ENABLE_BACKLIGHT"),
	SIGNAL_NOT_IMPLEMENTED("ENABLE_VCORE"),
	SIGNAL_NOT_IMPLEMENTED("ENABLE_VS"),
	SIGNAL_NOT_IMPLEMENTED("ENTERING_RW"),
	SIGNAL_NOT_IMPLEMENTED("PCH_A20GATE"),
	SIGNAL_NOT_IMPLEMENTED("PCH_DPWROK"),
	SIGNAL_NOT_IMPLEMENTED("PCH_HDA_SDO"),
	SIGNAL_NOT_IMPLEMENTED("PCH_LID_SWITCHn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_NMIn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_PWRBTNn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_PWROK"),
	SIGNAL_NOT_IMPLEMENTED("PCH_RCINn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_RSMRSTn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SMIn"),
	SIGNAL_NOT_IMPLEMENTED("PCH_SUSACKn"),
	SIGNAL_NOT_IMPLEMENTED("SHUNT_1_5V_DDR"),
	SIGNAL_NOT_IMPLEMENTED("USB1_CTL1"),
	SIGNAL_NOT_IMPLEMENTED("USB1_CTL2"),
	SIGNAL_NOT_IMPLEMENTED("USB1_CTL3"),
	SIGNAL_NOT_IMPLEMENTED("USB1_ENABLE"),
	SIGNAL_NOT_IMPLEMENTED("USB1_ILIM_SEL"),
	SIGNAL_NOT_IMPLEMENTED("USB2_CTL1"),
	SIGNAL_NOT_IMPLEMENTED("USB2_CTL2"),
	SIGNAL_NOT_IMPLEMENTED("USB2_CTL3"),
	SIGNAL_NOT_IMPLEMENTED("USB2_ENABLE"),
	SIGNAL_NOT_IMPLEMENTED("USB2_ILIM_SEL"),
};

#undef SIGNAL_NOT_IMPLEMENTED


/* Find a GPIO signal by name.  Returns the signal index, or GPIO_COUNT if
 * no match. */
static enum gpio_signal find_signal_by_name(const char *name)
{
	const struct gpio_info *g = signal_info;
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (!strcasecmp(name, g->name))
			return i;
	}

	return GPIO_COUNT;
}


/* Find the index of a GPIO port base address (LM4_GPIO_[A-Q]); this is used by
 * the clock gating registers.  Returns the index, or -1 if no match. */
static int find_gpio_port_index(uint32_t port_base)
{
	int i;
	for (i = 0; gpio_bases[i]; i++) {
		if (gpio_bases[i] == port_base)
			return i;
	}
	return -1;
}


int gpio_pre_init(void)
{
	/* Enable clocks to the GPIO blocks we use.  Bits are encoded this way;
	 * blocks we use are in caps: .qpn mlkj hgfe DCbA */
	LM4_SYSTEM_RCGCGPIO |= 0x000d;

	/* Turn off the LED before we make it an output */
	gpio_set_level(GPIO_DEBUG_LED, 0);

	/* Clear GPIOAFSEL bits for block A pin 7 */
	LM4_GPIO_AFSEL(LM4_GPIO_A) &= ~(0x80);

	/* Set GPIO to digital enable, output */
	LM4_GPIO_DEN(LM4_GPIO_A) |= 0x80;
	LM4_GPIO_DIR(LM4_GPIO_A) |= 0x80;

#ifdef BOARD_link
	/* Set up LID switch input (block K pin 5) */
	LM4_GPIO_PCTL(LM4_GPIO_K) &= ~(0xf00000);
	LM4_GPIO_DIR(LM4_GPIO_K) &= ~(0x20);
	LM4_GPIO_PUR(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_IM(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_IBE(LM4_GPIO_K) |= 0x20;

	/* Block F pin 0 is NMI pin, so we have to unlock GPIO Lock register and
	   set the bit in GPIOCR register first. */
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0x4c4f434b;
	LM4_GPIO_CR(LM4_GPIO_F) |= 0x1;
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0x0;

	/* Set up LID switch output (block F pin 0) */
	LM4_GPIO_PCTL(LM4_GPIO_F) &= ~(0xf);
	LM4_GPIO_DATA(LM4_GPIO_F, 0x1) =
		(LM4_GPIO_DATA(LM4_GPIO_K, 0x20) ? 1 : 0);
	LM4_GPIO_DIR(LM4_GPIO_F) |= 0x1;
	LM4_GPIO_DEN(LM4_GPIO_F) |= 0x1;
#endif

	/* Setup power button input and output pins */
#ifdef BOARD_link
	/* input: PK7 */
	LM4_GPIO_PCTL(LM4_GPIO_K) &= ~0xf0000000;
	LM4_GPIO_DIR(LM4_GPIO_K) &= ~0x80;
	LM4_GPIO_PUR(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_IM(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_IBE(LM4_GPIO_K) |= 0x80;
	/* output: PG7 */
	LM4_GPIO_PCTL(LM4_GPIO_G) &= ~0xf0000000;
	LM4_GPIO_DATA(LM4_GPIO_G, 0x80) = 0x80;
	LM4_GPIO_DIR(LM4_GPIO_G) |= 0x80;
	LM4_GPIO_DEN(LM4_GPIO_G) |= 0x80;
#else
	/* input: PC5 */
	LM4_GPIO_PCTL(LM4_GPIO_C) &= ~0x00f00000;
	LM4_GPIO_DIR(LM4_GPIO_C) &= ~0x20;
	LM4_GPIO_PUR(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_DEN(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_IM(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_IBE(LM4_GPIO_C) |= 0x20;
#endif

	return EC_SUCCESS;
}


void gpio_set_alternate_function(int port, int mask, int func)
{
	int port_index = find_gpio_port_index(port);
	int cgmask;

	if (port_index < 0)
		return;  /* TODO: assert */

	/* Enable the GPIO port if necessary */
	cgmask = 1 << port_index;
	if ((LM4_SYSTEM_RCGCGPIO & cgmask) != cgmask) {
		volatile uint32_t scratch  __attribute__((unused));
		LM4_SYSTEM_RCGCGPIO |= cgmask;
		/* Delay a few clocks before accessing GPIO registers on that
		 * port. */
		scratch = LM4_SYSTEM_RCGCGPIO;
	}

	if (func) {
		int pctlmask = 0;
		int i;
		/* Expand mask from bits to nibbles */
		for (i = 0; i < 8; i++) {
			if (mask & (1 << i))
				pctlmask |= 1 << (4 * i);
		}

		LM4_GPIO_PCTL(port) =
			(LM4_GPIO_PCTL(port) & ~(pctlmask * 0xf)) |
			(pctlmask * func);
		LM4_GPIO_AFSEL(port) |= mask;
	} else {
		LM4_GPIO_AFSEL(port) &= ~mask;
	}
	LM4_GPIO_DEN(port) |= mask;
}


int gpio_get_level(enum gpio_signal signal)
{
	return LM4_GPIO_DATA(signal_info[signal].port,
			     signal_info[signal].mask) ? 1 : 0;
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	/* Ok to write 0xff becuase LM4_GPIO_DATA bit-masks only the bit
	 * we care about. */
	LM4_GPIO_DATA(signal_info[signal].port,
		      signal_info[signal].mask) = (value ? 0xff : 0);
	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interrupt handlers */

static void gpio_interrupt(int port, uint32_t mis)
{
	int i = 0;
	const struct gpio_info *g = signal_info;

	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (port == g->port && (mis & g->mask) && g->irq_handler)
			g->irq_handler(i);
	}
}


static void __gpio_c_interrupt(void)
{
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_C);

	/* Clear the interrupt bits we received */
	LM4_GPIO_ICR(LM4_GPIO_C) = mis;

	gpio_interrupt(LM4_GPIO_C, mis);
}
DECLARE_IRQ(LM4_IRQ_GPIOC, __gpio_c_interrupt, 1);

/*****************************************************************************/
/* Console commands */

static int command_gpio_get(int argc, char **argv)
{
	const struct gpio_info *g = signal_info;
	int i;

	uart_puts("Current GPIO levels:\n");
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (g->mask)
			uart_printf("  %d %s\n", gpio_get_level(i), g->name);
		else
			uart_printf("  - %s\n", g->name);
		/* We'd overflow the output buffer without flushing */
		uart_flush_output();
	}
	return EC_SUCCESS;
}


static int command_gpio_set(int argc, char **argv)
{
	char *e;
	int v, i;

	if (argc < 3) {
		uart_puts("Usage: gpioset <signal_name> <0|1>\n");
		return EC_ERROR_UNKNOWN;
	}

	i = find_signal_by_name(argv[1]);
	if (i == GPIO_COUNT) {
		uart_puts("Unknown signal name.\n");
		return EC_ERROR_UNKNOWN;
	}

	if (!signal_info[i].mask) {
		uart_puts("Signal is not implemented; ignoring request.\n");
		return EC_SUCCESS;
	}

	v = strtoi(argv[2], &e, 0);
	if (*e) {
		uart_puts("Invalid signal value.\n");
		return EC_ERROR_UNKNOWN;
	}

	return gpio_set_level(i, v);
}


static const struct console_command console_commands[] = {
	{"gpioget", command_gpio_get},
	{"gpioset", command_gpio_set},
};
static const struct console_group command_group = {
	"GPIO", console_commands, ARRAY_SIZE(console_commands)
};

/*****************************************************************************/
/* Initialization */

int gpio_init(void)
{
	console_register_commands(&command_group);
	return EC_SUCCESS;
}
