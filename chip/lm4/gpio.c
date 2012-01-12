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


/* Raw flags for GPIO_INFO */
#define GI_OUTPUT      0x0001  /* Output */
#define GI_PULL        0x0002  /* Input with on-chip pullup/pulldown */
#define GI_HIGH        0x0004  /* If GI_OUTPUT, default high; if GI_PULL, pull
				* up (otherwise default low / pull down) */
#define GI_INT_RISING  0x0010  /* Interrupt on rising edge */
#define GI_INT_FALLING 0x0020  /* Interrupt on falling edge */
#define GI_INT_BOTH    0x0040  /* Interrupt on both edges */
#define GI_INT_LOW     0x0080  /* Interrupt on low level */
#define GI_INT_HIGH    0x0100  /* Interrupt on high level */
/* Common flag combinations */
#define GI_OUT_LOW     GI_OUTPUT
#define GI_OUT_HIGH    (GI_OUTPUT | GI_HIGH)
#define GI_PULL_DOWN   GI_PULL
#define GI_PULL_UP     (GI_PULL | GI_HIGH)
#define GI_INT_EDGE    (GI_INT_RISING | GI_INT_FALLING | GI_INT_BOTH)
#define GI_INT_LEVEL   (GI_INT_LOW | GI_INT_HIGH)
#define GI_INT_ANY     (GI_INT_EDGE | GI_INT_LEVEL)
/* Note that if no flags are present, the signal is a high-Z input */

struct gpio_info {
	const char *name;
	int port;   /* Port (LM4_GPIO_*) */
	int mask;   /* Bitmask on that port (0x01 - 0x80; 0x00 =
		       signal not implemented) */
	uint32_t flags;   /* Flags (GI_*) */
	void (*irq_handler)(enum gpio_signal signal);
};

/* Macro for signals which don't exist */
#define SIGNAL_NOT_IMPLEMENTED(name) {name, LM4_GPIO_A, 0, 0, NULL}

/* Signal information.  Must match order from enum gpio_signal. */
#ifdef BOARD_link
const struct gpio_info signal_info[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTONn",       LM4_GPIO_K, (1<<7), GI_INT_BOTH,
	 power_button_interrupt},
	{"LID_SWITCHn",         LM4_GPIO_K, (1<<5), GI_INT_BOTH,
	 power_button_interrupt},
	/* Other inputs */
	{"POWER_ONEWIRE",       LM4_GPIO_H, (1<<2), 0, NULL},
	{"THERMAL_DATA_READYn", LM4_GPIO_B, (1<<4), 0, NULL},
	{"AC_PRESENT",          LM4_GPIO_H, (1<<3), 0, NULL},
	{"PCH_BKLTEN",          LM4_GPIO_J, (1<<3), 0, NULL},
	{"PCH_SLP_An",          LM4_GPIO_G, (1<<5), 0, NULL},
	{"PCH_SLP_ME_CSW_DEVn", LM4_GPIO_G, (1<<4), 0, NULL},
	{"PCH_SLP_S3n",         LM4_GPIO_J, (1<<0), 0, NULL},
	{"PCH_SLP_S4n",         LM4_GPIO_J, (1<<1), 0, NULL},
	{"PCH_SLP_S5n",         LM4_GPIO_J, (1<<2), 0, NULL},
	{"PCH_SLP_SUSn",        LM4_GPIO_G, (1<<3), 0, NULL},
	{"PCH_SUSWARNn",        LM4_GPIO_G, (1<<2), 0, NULL},
	{"PGOOD_1_5V_DDR",      LM4_GPIO_K, (1<<0), 0, NULL},
	{"PGOOD_1_5V_PCH",      LM4_GPIO_K, (1<<1), 0, NULL},
	{"PGOOD_1_8VS",         LM4_GPIO_K, (1<<3), 0, NULL},
	{"PGOOD_5VALW",         LM4_GPIO_H, (1<<0), 0, NULL},
	{"PGOOD_CPU_CORE",      LM4_GPIO_M, (1<<3), 0, NULL},
	{"PGOOD_VCCP",          LM4_GPIO_K, (1<<2), 0, NULL},
	{"PGOOD_VCCSA",         LM4_GPIO_H, (1<<1), 0, NULL},
	{"PGOOD_VGFX_CORE",     LM4_GPIO_D, (1<<2), 0, NULL},
	{"RECOVERYn",           LM4_GPIO_H, (1<<7), 0, NULL},
	{"USB1_STATUSn",        LM4_GPIO_E, (1<<7), 0, NULL},
	{"USB2_STATUSn",        LM4_GPIO_E, (1<<1), 0, NULL},
	{"WRITE_PROTECTn",      LM4_GPIO_J, (1<<4), 0, NULL},
	/* Outputs; all unasserted by default */
	{"CPU_PROCHOTn",        LM4_GPIO_F, (1<<2), GI_OUT_HIGH, NULL},
	{"ENABLE_1_5V_DDR",     LM4_GPIO_H, (1<<5), GI_OUT_LOW, NULL},
	{"ENABLE_BACKLIGHT",    LM4_GPIO_H, (1<<4), GI_OUT_LOW, NULL},
	{"ENABLE_VCORE",        LM4_GPIO_F, (1<<7), GI_OUT_LOW, NULL},
	{"ENABLE_VS",           LM4_GPIO_G, (1<<6), GI_OUT_LOW, NULL},
	{"ENTERING_RW",         LM4_GPIO_J, (1<<5), GI_OUT_LOW, NULL},
	{"PCH_A20GATE",         LM4_GPIO_Q, (1<<6), GI_OUT_LOW, NULL},
	{"PCH_DPWROK",          LM4_GPIO_G, (1<<0), GI_OUT_LOW, NULL},
	{"PCH_HDA_SDO",         LM4_GPIO_G, (1<<1), GI_OUT_LOW, NULL},
	{"PCH_LID_SWITCHn",     LM4_GPIO_F, (1<<0), GI_OUT_HIGH, NULL},
	{"PCH_NMIn",            LM4_GPIO_M, (1<<2), GI_OUT_HIGH, NULL},
	{"PCH_PWRBTNn",         LM4_GPIO_G, (1<<7), GI_OUT_HIGH, NULL},
	{"PCH_PWROK",           LM4_GPIO_F, (1<<5), GI_OUT_LOW, NULL},
	{"PCH_RCINn",           LM4_GPIO_Q, (1<<7), GI_OUT_HIGH, NULL},
	/* Exception: RSMRST# is asserted at power-on */
	{"PCH_RSMRSTn",         LM4_GPIO_F, (1<<1), GI_OUT_LOW, NULL},
	{"PCH_SMIn",            LM4_GPIO_F, (1<<4), GI_OUT_HIGH, NULL},
	{"PCH_SUSACKn",         LM4_GPIO_F, (1<<3), GI_OUT_HIGH, NULL},
	{"SHUNT_1_5V_DDR",      LM4_GPIO_F, (1<<6), GI_OUT_HIGH, NULL},
	{"USB1_CTL1",           LM4_GPIO_E, (1<<2), GI_OUT_LOW, NULL},
	{"USB1_CTL2",           LM4_GPIO_E, (1<<3), GI_OUT_LOW, NULL},
	{"USB1_CTL3",           LM4_GPIO_E, (1<<4), GI_OUT_LOW, NULL},
	{"USB1_ENABLE",         LM4_GPIO_E, (1<<5), GI_OUT_LOW, NULL},
	{"USB1_ILIM_SEL",       LM4_GPIO_E, (1<<6), GI_OUT_LOW, NULL},
	{"USB2_CTL1",           LM4_GPIO_D, (1<<4), GI_OUT_LOW, NULL},
	{"USB2_CTL2",           LM4_GPIO_D, (1<<5), GI_OUT_LOW, NULL},
	{"USB2_CTL3",           LM4_GPIO_D, (1<<6), GI_OUT_LOW, NULL},
	{"USB2_ENABLE",         LM4_GPIO_D, (1<<7), GI_OUT_LOW, NULL},
	{"USB2_ILIM_SEL",       LM4_GPIO_E, (1<<0), GI_OUT_LOW, NULL},
};

#else
const struct gpio_info signal_info[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTONn", LM4_GPIO_C, (1<<5), GI_PULL_UP | GI_INT_BOTH,
	 power_button_interrupt},
	{"LID_SWITCHn",   LM4_GPIO_D, (1<<0), GI_PULL_UP | GI_INT_BOTH,
	 power_button_interrupt},
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
	{"DEBUG_LED",    LM4_GPIO_A, (1<<7), GI_OUT_LOW, NULL},
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

#endif

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
	volatile uint32_t scratch  __attribute__((unused));
	const struct gpio_info *g = signal_info;
	int i;

	/* Enable clocks to all the GPIO blocks (since we use all of them as
	 * GPIOs) */
	LM4_SYSTEM_RCGCGPIO |= 0x7fff;
	scratch = LM4_SYSTEM_RCGCGPIO;  /* Delay a few clocks */

	/* Disable GPIO commit control for PD7 and PF0, since we don't use the
	 * NMI pin function. */
	LM4_GPIO_LOCK(LM4_GPIO_D) = LM4_GPIO_LOCK_UNLOCK;
	LM4_GPIO_CR(LM4_GPIO_D) |= 0x80;
	LM4_GPIO_LOCK(LM4_GPIO_D) = 0;
	LM4_GPIO_LOCK(LM4_GPIO_F) = LM4_GPIO_LOCK_UNLOCK;
	LM4_GPIO_CR(LM4_GPIO_F) |= 0x01;
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0;

	/* Clear SSI0 alternate function on PA2:5 */
	LM4_GPIO_AFSEL(LM4_GPIO_A) &= ~0x3c;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {

		/* Handle GPIO direction */
		if (g->flags & GI_OUTPUT) {
			/* Output with default level */
			gpio_set_level(i, g->flags & GI_HIGH);
			LM4_GPIO_DIR(g->port) |= g->mask;
		} else {
			/* Input */
			if (g->flags & GI_PULL) {
				/* With pull up/down */
				if (g->flags & GI_HIGH)
					LM4_GPIO_PUR(g->port) |= g->mask;
				else
					LM4_GPIO_PDR(g->port) |= g->mask;
			}
		}

		/* Use as GPIO, not alternate function */
		gpio_set_alternate_function(g->port, g->mask, 0);

		/* Set up interrupts if necessary */
		if (g->flags & GI_INT_LEVEL)
			LM4_GPIO_IS(g->port) |= g->mask;
		if (g->flags & (GI_INT_RISING | GI_INT_HIGH))
			LM4_GPIO_IEV(g->port) |= g->mask;
		if (g->flags & GI_INT_BOTH)
			LM4_GPIO_IBE(g->port) |= g->mask;
		if (g->flags & GI_INT_ANY)
			LM4_GPIO_IM(g->port) |= g->mask;
	}

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

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = find_signal_by_name(argv[1]);
		if (i == GPIO_COUNT) {
			uart_puts("Unknown signal name.\n");
			return EC_ERROR_UNKNOWN;
		}
		g = signal_info + i;
		uart_printf("  %d %s\n", gpio_get_level(i), g->name);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	uart_puts("Current GPIO levels:\n");
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		if (g->mask)
			uart_printf("  %d %s\n", gpio_get_level(i), g->name);
		/* We have enough GPIOs that we'll overflow the output buffer
		 * without flushing */
		uart_flush_output();
	}
	return EC_SUCCESS;
}


static int command_gpio_set(int argc, char **argv)
{
	const struct gpio_info *g;
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
	g = signal_info + i;

	if (!g->mask) {
		uart_puts("Signal is not implemented.\n");
		return EC_ERROR_UNKNOWN;
	}
	if (!(g->flags & GI_OUTPUT)) {
		uart_puts("Signal is not an output.\n");
		return EC_ERROR_UNKNOWN;
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
