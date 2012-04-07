/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "board.h"
#include "chipset.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "lightbar.h"
#include "lpc.h"
#include "pwm.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "x86_power.h"

/* Default timeout in us; if we've been waiting this long for an input
 * transition, just jump to the next state. */
#define DEFAULT_TIMEOUT 1000000

enum x86_state {
	X86_G3 = 0,                 /* Initial state */
	X86_S5,                     /* System is off */
	X86_S3,                     /* RAM is on; processor is asleep */
	X86_S0,                     /* System is on */

	/* Transitions */
	X86_G3S5,                   /* G3 -> S5 (at system init time) */
	X86_S5S3,                   /* S5 -> S3 */
	X86_S3S0,                   /* S3 -> S0 */
	X86_S0S3,                   /* S0 -> S3 */
	X86_S3S5,                   /* S3 -> S5 */
};

static const char * const state_names[] = {
	"G3",
	"S5",
	"S3",
	"S0",
	"G3->S5",
	"S5->S3",
	"S3->S0",
	"S0->S3",
	"S3->S5",
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
/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALWAYS_ON | IN_PGOOD_ALL_NONCORE |		\
		   IN_PGOOD_CPU_CORE | IN_ALL_PM_SLP_DEASSERTED)

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
		if (task_wait_event(DEFAULT_TIMEOUT) == TASK_EVENT_TIMER) {
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


void x86_power_cpu_overheated(int too_hot)
{
	/* TODO: crosbug.com/p/8242 - real implementation */
}


void x86_power_force_shutdown(void)
{
	/* TODO: crosbug.com/p/8242 - real implementation */
}


void x86_power_reset(int cold_reset)
{
	if (cold_reset) {
		/* Drop and restore PWROK.  This causes the PCH to reboot,
		 * regardless of its after-G3 setting.  This type of reboot
		 * causes the PCH to assert PLTRST#, SLP_S3#, and SLP_S5#, so
		 * we actually drop power to the rest of the system (hence, a
		 * "cold" reboot). */

		/* Ignore if PWROK is already low */
		if (gpio_get_level(GPIO_PCH_PWROK) == 0)
			return;

		/* PWROK must deassert for at least 3 RTC clocks = 91 us */
		gpio_set_level(GPIO_PCH_PWROK, 0);
		udelay(100);
		gpio_set_level(GPIO_PCH_PWROK, 1);

	} else {
		/* Send a RCIN# pulse to the PCH.  This just causes it to
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system. */

		/* Ignore if RCINn is already low */
		if (gpio_get_level(GPIO_PCH_RCINn) == 0)
			return;

		/* Pulse must be at least 16 PCI clocks long = 500 ns */
		gpio_set_level(GPIO_PCH_RCINn, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCINn, 1);
	}
}

/*****************************************************************************/
/* Chipset interface */

/* Returns non-zero if the chipset is in the specified state. */
int chipset_in_state(enum chipset_state in_state)
{
	switch (in_state) {
	case CHIPSET_STATE_SOFT_OFF:
		return (state == X86_S5);
	case CHIPSET_STATE_SUSPEND:
		return (state == X86_S3);
	case CHIPSET_STATE_ON:
		return (state == X86_S0);
	}

	/* Should never get here since we list all states above, but compiler
	 * doesn't seem to understand that. */
	return 0;
}

/*****************************************************************************/
/* Interrupts */

void x86_power_interrupt(enum gpio_signal signal)
{
	/* Shadow signals and compare with our desired signal state. */
	update_in_signals();

	/* Wake up the task */
	task_wake(TASK_ID_X86POWER);
}

/*****************************************************************************/
/* Initialization */

int x86_power_init(void)
{
	/* Default to G3 state unless proven otherwise */
	state = X86_G3;

	/* Update input state */
	update_in_signals();
	in_want = 0;

	/* If we're switching between images without rebooting, see if the x86
	 * is already powered on; if so, leave it there instead of cycling
	 * through G3. */
	if (system_jumped_to_this_image()) {
		if ((in_signals & IN_ALL_S0) == IN_ALL_S0) {
			uart_puts("[x86 already in S0]\n");
			state = X86_S0;
		} else {
			/* Force all signals to their G3 states */
			uart_puts("[x86 forcing G3]\n");
			gpio_set_level(GPIO_PCH_PWROK, 0);
			gpio_set_level(GPIO_ENABLE_VCORE, 0);
			gpio_set_level(GPIO_PCH_RCINn, 0);
			gpio_set_level(GPIO_ENABLE_VS, 0);
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn, 0);
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 0);
			gpio_set_level(GPIO_SHUNT_1_5V_DDR, 1);
			gpio_set_level(GPIO_PCH_RSMRSTn, 0);
			gpio_set_level(GPIO_PCH_DPWROK, 0);
		}
	}

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
		uart_printf("[%T x86 power state %d = %s, in 0x%04x]\n",
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


		case X86_S5S3:
			/* Turn on power to RAM */
			gpio_set_level(GPIO_SHUNT_1_5V_DDR, 0);
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 1);

			/* Enable touchpad power and take touchscreen out of
			 * reset, so they can wake the system from suspend. */
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn, 1);

			state = X86_S3;
			break;

		case X86_S3S0:
			/* Deassert RCINn */
			gpio_set_level(GPIO_PCH_RCINn, 1);

			/* Mask all host events until the host unmasks
			 * them itself.  */
			lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
			lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
			lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);

			/* Turn on power rails */
			gpio_set_level(GPIO_ENABLE_VS, 1);

			/* Enable fan, now that +5VS is turned on */
			/* TODO: On proto1+, fan is on +5VALW, so we can leave
			 * it on all the time. */
			pwm_enable_fan(1);

			/* Wait for non-core power rails good */
			wait_in_signals(IN_PGOOD_ALL_NONCORE);

			/* Enable +CPU_CORE and +VGFX_CORE regulator.  The CPU
			 * itself will request the supplies when it's ready. */
			gpio_set_level(GPIO_ENABLE_VCORE, 1);

			/* Wait 99ms after all voltages good */
			usleep(99000);

			/* Set PCH_PWROK */
			gpio_set_level(GPIO_PCH_PWROK, 1);

			state = X86_S0;

			lightbar_sequence(LIGHTBAR_S3S0);
			break;

		case X86_S0S3:
			lightbar_sequence(LIGHTBAR_S0S3);

			/* Clear PCH_PWROK */
			gpio_set_level(GPIO_PCH_PWROK, 0);

			/* Wait 40ns */
			udelay(1);

			/* Disable +CPU_CORE and +VGFX_CORE */
			gpio_set_level(GPIO_ENABLE_VCORE, 0);

			/* Assert RCINn */
			gpio_set_level(GPIO_PCH_RCINn, 0);

			/* Disable fan, since it's powered by +5VS */
			/* TODO: On proto1+, fan is on +5VALW, so we can leave
			 * it on all the time. */
			pwm_enable_fan(0);

			/* Turn off power rails */
			gpio_set_level(GPIO_ENABLE_VS, 0);

			state = X86_S3;
			break;

		case X86_S3S5:
			/* Disable touchpad power and reset touchscreen. */
			gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);
			gpio_set_level(GPIO_TOUCHSCREEN_RESETn, 0);

			/* Turn off power to RAM */
			gpio_set_level(GPIO_ENABLE_1_5V_DDR, 0);
			gpio_set_level(GPIO_SHUNT_1_5V_DDR, 1);

			state = X86_S5;
			break;

		case X86_S5:
			if (gpio_get_level(GPIO_PCH_SLP_S5n) == 1) {
				/* Power up to next state */
				state = X86_S5S3;
				break;
			}

			/* Otherwise, steady state; wait for a message */
			in_want = 0;
			task_wait_event(-1);
			break;

		case X86_S3:
			if (gpio_get_level(GPIO_PCH_SLP_S3n) == 1) {
				/* Power up to next state */
				state = X86_S3S0;
				break;
			} else if (gpio_get_level(GPIO_PCH_SLP_S5n) == 0) {
				/* Power down to next state */
				state = X86_S3S5;
				break;
			}

			/* Otherwise, steady state; wait for a message */
			in_want = 0;
			task_wait_event(-1);
			break;

		case X86_S0:
			if (gpio_get_level(GPIO_PCH_SLP_S3n) == 0) {
				/* Power down to next state */
				state = X86_S0S3;
				break;
			}

			/* Otherwise, steady state; wait for a message */
			in_want = 0;
			task_wait_event(-1);
		}
	}
}

/*****************************************************************************/
/* Console commnands */

static int command_x86power(int argc, char **argv)
{
	/* Print current state */
	uart_printf("Current X86 state: %d (%s)\n", state, state_names[state]);

	/* Forcing a power state from EC is deprecated */
	if (argc > 1)
		uart_puts("Use 'powerbtn' instead of 'x86power s0'.\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(x86power, command_x86power);


static int command_x86reset(int argc, char **argv)
{
	int is_cold = 1;

	if (argc > 1 && !strcasecmp(argv[1], "cold"))
		is_cold = 1;
	else if (argc > 1 && !strcasecmp(argv[1], "warm"))
		is_cold = 0;

	/* Force the x86 to reset */
	uart_printf("Issuing x86 %s reset...\n", is_cold ? "cold" : "warm");
	x86_power_reset(is_cold);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(x86reset, command_x86reset);
