/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "board.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "x86_power.h"

/* Default timeout in us; if we've been waiting this long for an input
 * transition, just jump to the next state. */
#define DEFAULT_TIMEOUT 1000000

enum x86_state {
	/* Stable states */
	X86_G3 = 0,                 /* Initial state */
	X86_S5,                     /* System is off */
	X86_S0,                     /* System is on */

	/* Transitions */
	X86_G3S5,                   /* G3 -> S5 (at system init time) */
	X86_S5S0,                   /* S5 -> S0 */

	/* TODO: S3 state, S0S5, S0S3, S3S0 */
};

static const char * const state_names[] = {
	"G3",
	"S5",
	"S0",
	"G3->S5",
	"S5->S0",
};

/* Input state flags */
#define IN_PGOOD_5VALW             0x0001
#define IN_PGOOD_1_5V_DDR          0x0002
#define IN_PGOOD_1_5V_PCH          0x0004
#define IN_PGOOD_1_8VS             0x0008
#define IN_PGOOD_VCCP              0x0010
#define IN_PGOOD_VCCSA             0x0020
#define IN_PGOOD_CPU_CORE          0x0040
#define IN_PGOOD_VGFX_CORE         0x0080
#define IN_PCH_SLP_S3n_DEASSERTED  0x0100
#define IN_PCH_SLP_S4n_DEASSERTED  0x0200
#define IN_PCH_SLP_S5n_DEASSERTED  0x0400
#define IN_PCH_SLP_An_DEASSERTED   0x0800
#define IN_PCH_SLP_SUSn_DEASSERTED 0x1000
#define IN_PCH_SLP_MEn_DEASSERTED  0x2000
#define IN_PCH_SUSWARNn_DEASSERTED 0x4000
#define IN_PCH_BKLTEN_ASSERTED     0x8000
/* All always-on supplies */
#define IN_PGOOD_ALWAYS_ON   (IN_PGOOD_5VALW)
/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_1_5V_DDR | IN_PGOOD_1_5V_PCH |	\
			      IN_PGOOD_1_8VS | IN_PGOOD_VCCP | IN_PGOOD_VCCSA)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_CPU_CORE | IN_PGOOD_VGFX_CORE)
/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3n_DEASSERTED |		\
				  IN_PCH_SLP_S4n_DEASSERTED |		\
				  IN_PCH_SLP_S5n_DEASSERTED |		\
				  IN_PCH_SLP_An_DEASSERTED)


static enum x86_state state;  /* Current state */
static uint32_t in_signals;   /* Current input signal states (IN_PGOOD_*) */
static uint32_t in_want;      /* Input signal state we're waiting for */


/* Update input signal state */
static void update_in_signals(void)
{
	uint32_t inew = 0;
	int v;

	if (gpio_get_level(GPIO_PGOOD_5VALW))
		inew |= IN_PGOOD_5VALW;

	if (gpio_get_level(GPIO_PGOOD_1_5V_DDR))
		inew |= IN_PGOOD_1_5V_DDR;
	if (gpio_get_level(GPIO_PGOOD_1_5V_PCH))
		inew |= IN_PGOOD_1_5V_PCH;
	if (gpio_get_level(GPIO_PGOOD_1_8VS))
		inew |= IN_PGOOD_1_8VS;
	if (gpio_get_level(GPIO_PGOOD_VCCP))
		inew |= IN_PGOOD_VCCP;
	if (gpio_get_level(GPIO_PGOOD_VCCSA))
		inew |= IN_PGOOD_VCCSA;

	if (gpio_get_level(GPIO_PGOOD_CPU_CORE))
		inew |= IN_PGOOD_CPU_CORE;
	if (gpio_get_level(GPIO_PGOOD_VGFX_CORE))
		inew |= IN_PGOOD_VGFX_CORE;

	if (gpio_get_level(GPIO_PCH_SLP_An))
		inew |= IN_PCH_SLP_An_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S3n))
		inew |= IN_PCH_SLP_S3n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S4n))
		inew |= IN_PCH_SLP_S4n_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_S5n))
		inew |= IN_PCH_SLP_S5n_DEASSERTED;

	if (gpio_get_level(GPIO_PCH_SLP_SUSn))
		inew |= IN_PCH_SLP_SUSn_DEASSERTED;
	if (gpio_get_level(GPIO_PCH_SLP_ME_CSW_DEVn))
		inew |= IN_PCH_SLP_MEn_DEASSERTED;

	v = gpio_get_level(GPIO_PCH_SUSWARNn);
	if (v)
		inew |= IN_PCH_SUSWARNn_DEASSERTED;
	/* Copy SUSWARN# signal from PCH to SUSACK# */
	gpio_set_level(GPIO_PCH_SUSACKn, v);

	v = gpio_get_level(GPIO_PCH_BKLTEN);
	if (v)
		inew |= IN_PCH_BKLTEN_ASSERTED;
	/* Copy backlight enable signal from PCH to BKLTEN */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, v);

	in_signals = inew;
}


/* Wait for all the inputs in <want> to be present.  Returns EC_ERROR_TIMEOUT
 * if timeout before reaching the desired state. */
static int wait_in_signals(uint32_t want)
{
	in_want = want;

	while ((in_signals & in_want) != in_want) {
		if (task_wait_msg(DEFAULT_TIMEOUT) == (1 << TASK_ID_TIMER)) {
			update_in_signals();
			uart_printf("[x86 power timeout on input; "
				    "wanted 0x%04x, got 0x%04x]\n",
				    in_want, in_signals & in_want);
			return EC_ERROR_TIMEOUT;
		}
		/* TODO: should really shrink the remaining timeout if we woke
		 * up but didn't have all the signals we wanted.  Also need to
		 * handle aborts if we're no longer in the same state we were
		 * when we started waiting. */
	}
	return EC_SUCCESS;
}


/*****************************************************************************/
/* Interrupts */

void x86_power_interrupt(enum gpio_signal signal)
{
	/* Shadow signals and compare with our desired signal state. */
	update_in_signals();

	/* Wake task if we want at least one signal, and all all the inputs we
	 * want are present */
	if (in_want && (in_signals & in_want) == in_want)
		task_send_msg(TASK_ID_X86POWER, TASK_ID_X86POWER, 0);
}

/*****************************************************************************/
/* Initialization */

int x86_power_init(void)
{
	state = X86_G3;

	/* Update input state */
	update_in_signals();
	in_want = 0;

	/* Enable interrupts for our GPIOs */
	gpio_enable_interrupt(GPIO_PCH_BKLTEN);
	gpio_enable_interrupt(GPIO_PCH_SLP_An);
	gpio_enable_interrupt(GPIO_PCH_SLP_ME_CSW_DEVn);
	gpio_enable_interrupt(GPIO_PCH_SLP_S3n);
	gpio_enable_interrupt(GPIO_PCH_SLP_S4n);
	gpio_enable_interrupt(GPIO_PCH_SLP_S5n);
	gpio_enable_interrupt(GPIO_PCH_SLP_SUSn);
	gpio_enable_interrupt(GPIO_PCH_SUSWARNn);
	gpio_enable_interrupt(GPIO_PGOOD_1_5V_DDR);
	gpio_enable_interrupt(GPIO_PGOOD_1_5V_PCH);
	gpio_enable_interrupt(GPIO_PGOOD_1_8VS);
	gpio_enable_interrupt(GPIO_PGOOD_5VALW);
	gpio_enable_interrupt(GPIO_PGOOD_CPU_CORE);
	gpio_enable_interrupt(GPIO_PGOOD_VCCP);
	gpio_enable_interrupt(GPIO_PGOOD_VCCSA);
	gpio_enable_interrupt(GPIO_PGOOD_VGFX_CORE);

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Task function */

void x86_power_task(void)
{
	x86_power_init();

	while (1) {
		uart_printf("[x86 power state %d = %s, in 0x%04x]\n",
			    state, state_names[state], in_signals);

		switch (state) {

		case X86_G3:
			/* Move to S5 state on boot */
			state = X86_G3S5;
			break;

		case X86_G3S5:
			/* Wait for the always-on rails to be good */
			wait_in_signals(IN_PGOOD_ALWAYS_ON);

			/* Wait 10ms after +5VALW good */
			usleep(10000);

			/* Assert DPWROK, deassert RSMRST# */
			gpio_set_level(GPIO_PCH_DPWROK, 1);
			gpio_set_level(GPIO_PCH_RSMRSTn, 1);

			/* Wait 5ms for SUSCLK to stabilize */
			usleep(5000);

			state = X86_S5;
			break;

		case X86_S5S0:
			/* TODO: this should be in response to a power button
			 * event, not causing one.  For initial bringup,
			 * simulate the event. */
			/* Assert power button */
			gpio_set_level(GPIO_PCH_PWRBTNn, 0);
			/* Wait 16ms after asserting PWRBTN# */
			usleep(16000);
			/* Release power button */
			gpio_set_level(GPIO_PCH_PWRBTNn, 1);

			/* Wait for PM_SLP_S3n to be asserted */
			wait_in_signals(IN_ALL_PM_SLP_DEASSERTED);

			/* Turn on power rails */
			gpio_set_level(GPIO_ENABLE_VS, 1);
			gpio_set_level(GPIO_SHUNT_1_5V_DDR, 0);
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 1);

			/* Wait for non-core power rails good */
			wait_in_signals(IN_PGOOD_ALL_NONCORE);

			/* Enable +CPU_CORE and +VGFX_CORE */
			gpio_set_level(GPIO_ENABLE_VCORE, 1);

			/* Wait for all supplies good */
			wait_in_signals(IN_PGOOD_ALL_NONCORE |
					IN_PGOOD_ALL_CORE);

			/* Wait 99ms after all voltages good */
			usleep(99000);

			/* Set PCH_PWROK */
			gpio_set_level(GPIO_PCH_PWROK, 1);

			state = X86_S0;
			break;

		case X86_S5:
#ifdef AUTO_POWER_UP
			/* For bringup, power on one second after boot */
			/* TODO: remove post-bringup */
			usleep(1000000);
			state = X86_S5S0;
			break;
#endif
		case X86_S0:
			/* Steady state; wait for a message */
			task_wait_msg(-1);
		}
	}
}

/*****************************************************************************/
/* Console commnands */

static int command_x86power(int argc, char **argv)
{
	enum x86_state current = state;
	/* If no args provided, print current state */
	if (argc < 2) {
		uart_printf("Current X86 state: %d (%s)\n",
			    state, state_names[state]);
		return EC_SUCCESS;
	}

	/* Get state to move to */
	if (!strcasecmp(argv[1], "S0")) {
		if (state == X86_S5)
			state = X86_S5S0;
	}

	if (current == state)
		uart_puts("State not changed.\n");
	else {
		uart_printf("New X86 state: %d (%s)\n",
			    state, state_names[state]);
		/* Wake up the task if it's asleep */
		task_send_msg(TASK_ID_X86POWER, TASK_ID_X86POWER, 0);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(x86power, command_x86power);
