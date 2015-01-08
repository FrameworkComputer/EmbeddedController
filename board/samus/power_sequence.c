/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* X86 chipset power control module for Chrome EC */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "wireless.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/* Input state flags */
#define IN_PGOOD_PP1050           POWER_SIGNAL_MASK(X86_PGOOD_PP1050)
#define IN_PGOOD_PP1200           POWER_SIGNAL_MASK(X86_PGOOD_PP1200)
#define IN_PGOOD_PP1800           POWER_SIGNAL_MASK(X86_PGOOD_PP1800)
#define IN_PGOOD_VCORE            POWER_SIGNAL_MASK(X86_PGOOD_VCORE)

#define IN_PCH_SLP_S0_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S0_DEASSERTED)
#define IN_PCH_SLP_S3_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S3_DEASSERTED)
#define IN_PCH_SLP_S5_DEASSERTED  POWER_SIGNAL_MASK(X86_SLP_S5_DEASSERTED)
#define IN_PCH_SLP_SUS_DEASSERTED POWER_SIGNAL_MASK(X86_SLP_SUS_DEASSERTED)


/* All non-core power rails */
#define IN_PGOOD_ALL_NONCORE (IN_PGOOD_PP1050)
/* All core power rails */
#define IN_PGOOD_ALL_CORE    (IN_PGOOD_VCORE)
/* Rails required for S3 */
#define IN_PGOOD_S3          (IN_PGOOD_PP1200)
/* Rails required for S0 */
#define IN_PGOOD_S0          (IN_PGOOD_ALL_NONCORE)
/* Rails used to detect if PP5000 is up. 1.8V PGOOD is not
 * a reliable signal to use here with an internal pullup. */
#define IN_PGOOD_PP5000	     (IN_PGOOD_PP1050 | IN_PGOOD_PP1200)


/* All PM_SLP signals from PCH deasserted */
#define IN_ALL_PM_SLP_DEASSERTED (IN_PCH_SLP_S3_DEASSERTED | \
				  IN_PCH_SLP_S5_DEASSERTED | \
				  IN_PCH_SLP_SUS_DEASSERTED)

/* All inputs in the right state for S0 */
#define IN_ALL_S0 (IN_PGOOD_ALL_NONCORE | IN_PGOOD_ALL_CORE | \
		   IN_ALL_PM_SLP_DEASSERTED)

static int throttle_cpu;      /* Throttle CPU? */
static int pause_in_s5;	      /* Pause in S5 when shutting down? */
static uint32_t pp5000_in_g3; /* Turn PP5000 on in G3? */

void chipset_force_shutdown(void)
{
	CPRINTS("%s()", __func__);

	/*
	 * Force off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
}

static void chipset_force_g3(void)
{
	CPRINTS("Forcing G3");

	gpio_set_level(GPIO_PCH_PWROK, 0);
	gpio_set_level(GPIO_SYS_PWROK, 0);
	gpio_set_level(GPIO_PP1050_EN, 0);
	gpio_set_level(GPIO_PP1200_EN, 0);
	gpio_set_level(GPIO_PP1800_EN, 0);
	gpio_set_level(GPIO_PP3300_DSW_GATED_EN, 0);
	gpio_set_level(GPIO_PP5000_USB_EN, 0);
	/* Disable PP5000 if allowed */
	if (!pp5000_in_g3)
		gpio_set_level(GPIO_PP5000_EN, 0);
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);
	gpio_set_level(GPIO_PCH_DPWROK, 0);
	gpio_set_level(GPIO_PP3300_DSW_EN, 0);
	wireless_set_state(WIRELESS_OFF);
}

static void chipset_reset_rtc(void)
{
	/*
	 * Assert RTCRST# to the PCH long enough for it to latch the
	 * assertion and reset the internal RTC backed state.
	 */
	CPRINTS("Asserting RTCRST# to PCH");
	gpio_set_level(GPIO_PCH_RTCRST_L, 0);
	udelay(100);
	gpio_set_level(GPIO_PCH_RTCRST_L, 1);
	udelay(10 * MSEC);
}

void chipset_reset(int cold_reset)
{
	CPRINTS("%s(%d)", __func__, cold_reset);
	if (cold_reset) {
		/*
		 * Drop and restore PWROK.  This causes the PCH to reboot,
		 * regardless of its after-G3 setting.  This type of reboot
		 * causes the PCH to assert PLTRST#, SLP_S3#, and SLP_S5#, so
		 * we actually drop power to the rest of the system (hence, a
		 * "cold" reboot).
		 */

		/* Ignore if PWROK is already low */
		if (gpio_get_level(GPIO_PCH_PWROK) == 0)
			return;

		/* PWROK must deassert for at least 3 RTC clocks = 91 us */
		gpio_set_level(GPIO_PCH_PWROK, 0);
		udelay(100);
		gpio_set_level(GPIO_PCH_PWROK, 1);

	} else {
		/*
		 * Send a RCIN# pulse to the PCH.  This just causes it to
		 * assert INIT# to the CPU without dropping power or asserting
		 * PLTRST# to reset the rest of the system.
		 */

		/*
		 * Pulse must be at least 16 PCI clocks long = 500 ns.
		 */
		gpio_set_level(GPIO_PCH_RCIN_L, 0);
		udelay(10);
		gpio_set_level(GPIO_PCH_RCIN_L, 1);
	}
}

void chipset_throttle_cpu(int throttle)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_set_level(GPIO_CPU_PROCHOT, throttle);
}

enum power_state power_chipset_init(void)
{
	/*
	 * If we're switching between images without rebooting, see if the x86
	 * is already powered on; if so, leave it there instead of cycling
	 * through G3.
	 */
	if (system_jumped_to_this_image()) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			/* Disable idle task deep sleep when in S0. */
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
			return POWER_S0;
		} else {
			/* Force all signals to their G3 states */
			chipset_force_g3();
		}
	}

	return POWER_G3;
}

static void update_touchscreen(void)
{
	/*
	 * If the lid is closed; put the touchscreen in reset to save power.
	 * If the lid is open, take it out of reset so it can wake the
	 * processor (although just opening the lid should do that anyway, so
	 * we don't have to worry about it staying on while the AP is off).
	 */
	gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, lid_is_open());
}
DECLARE_HOOK(HOOK_LID_CHANGE, update_touchscreen, HOOK_PRIO_DEFAULT);

enum power_state power_handle_state(enum power_state state)
{
	struct batt_params batt;

	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:

		while ((power_get_signals() & IN_PCH_SLP_S5_DEASSERTED) == 0) {
			if (task_wait_event(SECOND*4) == TASK_EVENT_TIMER) {
				CPRINTS("timeout waiting for S5 exit");
				/* Put system in G3 and assert RTCRST# */
				chipset_force_g3();
				chipset_reset_rtc();
				/* Try to power back up after RTC reset */
				return POWER_G3S5;
			}
		}
		return POWER_S5S3; /* Power up to next state */
		break;

	case POWER_S3:
		/* Check for state transitions */
		if (!power_has_signals(IN_PGOOD_S3)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S3S5;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 1) {
			/* Power up to next state */
			return POWER_S3S0;
		} else if (gpio_get_level(GPIO_PCH_SLP_S5_L) == 0) {
			/* Power down to next state */
			return POWER_S3S5;
		}
		break;

	case POWER_S0:
		if (!power_has_signals(IN_PGOOD_S0)) {
			/* Required rail went away */
			chipset_force_shutdown();
			return POWER_S0S3;
		} else if (gpio_get_level(GPIO_PCH_SLP_S3_L) == 0) {
			/* Power down to next state */
			return POWER_S0S3;
		}
		break;

	case POWER_G3S5:
		/* Return to G3 if battery level is too low */
		if (charge_want_shutdown() ||
		    charge_prevent_power_on()) {
			CPRINTS("power-up inhibited");
			chipset_force_g3();
			return POWER_G3;
		}

		/* Enable 3.3V DSW */
		gpio_set_level(GPIO_PP3300_DSW_EN, 1);

		/*
		 * Wait 10ms after +3VALW good, since that powers VccDSW and
		 * VccSUS.
		 */
		msleep(10);

		/* Enable PP5000 (5V) rail as 1.05V and 1.2V rails need 5V
		 * rail to regulate properly. */
		gpio_set_level(GPIO_PP5000_EN, 1);

		/* Wait for PP1050/PP1200 PGOOD to go LOW to
		 * indicate that PP5000 is stable */
		while ((power_get_signals() & IN_PGOOD_PP5000) != 0) {
			if (task_wait_event(SECOND) == TASK_EVENT_TIMER) {
				CPRINTS("timeout waiting for PP5000");
				chipset_force_g3();
				return POWER_G3;
			}
		}

		/* Assert DPWROK */
		gpio_set_level(GPIO_PCH_DPWROK, 1);

		/*
		 * Wait for SLP_SUS before enabling 1.05V rail.
		 */
		if (power_wait_signals(IN_PCH_SLP_SUS_DEASSERTED)) {
			CPRINTS("timeout waiting for SLP_SUS deassert");
			chipset_force_g3();
			return POWER_G3;
		}

		/* Enable PP1050 rail. */
		gpio_set_level(GPIO_PP1050_EN, 1);

		/* Wait for 1.05V to come up and CPU to notice */
		if (power_wait_signals(IN_PGOOD_PP1050)) {
			CPRINTS("timeout waiting for PP1050");
			chipset_force_g3();
			return POWER_G3;
		}

		/* Add 10ms delay between SUSP_VR and RSMRST */
		msleep(10);

		/* Deassert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 1);

		/* Wait 5ms for SUSCLK to stabilize */
		msleep(5);

		/* Call hook to indicate out of G3 state */
		hook_notify(HOOK_CHIPSET_PRE_INIT);
		return POWER_S5;

	case POWER_S5S3:
		/*
		 * TODO(crosbug.com/p/31583): Temporary hack to allow booting
		 * without battery. If battery is not present here, then delay
		 * to give time for PD MCU to negotiate to 20V.
		 */
		battery_get_params(&batt);
		if (batt.is_present != BP_YES && !system_is_locked()) {
			CPRINTS("Attempting boot w/o battery, adding delay");
			msleep(500);
		}

		/* Turn on power to RAM */
		gpio_set_level(GPIO_PP1800_EN, 1);
		gpio_set_level(GPIO_PP1200_EN, 1);
		if (power_wait_signals(IN_PGOOD_S3)) {
			gpio_set_level(GPIO_PP1800_EN, 0);
			gpio_set_level(GPIO_PP1200_EN, 0);
			chipset_force_shutdown();
			return POWER_S5;
		}

		/*
		 * Take lightbar out of reset, now that +5VALW is
		 * available and we won't leak +3VALW through the reset
		 * line.
		 */
		gpio_set_level(GPIO_LIGHTBAR_RESET_L, 1);

		/*
		 * Enable touchpad power so it can wake the system from
		 * suspend.
		 */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 1);

		/* Turn on USB power rail. */
		gpio_set_level(GPIO_PP5000_USB_EN, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);
		return POWER_S3;

	case POWER_S3S0:
		/* Turn on 3.3V DSW gated rail for core regulator */
		gpio_set_level(GPIO_PP3300_DSW_GATED_EN, 1);

		/* Wait 20ms before allowing VCCST_PGOOD to rise. */
		msleep(20);

		/* Enable wireless. */
		wireless_set_state(WIRELESS_ON);

		/* Make sure the touchscreen is on, too. */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 1);

		/* Wait for non-core power rails good */
		if (power_wait_signals(IN_PGOOD_S0)) {
			gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
			wireless_set_state(WIRELESS_OFF);
			gpio_set_level(GPIO_PP3300_DSW_GATED_EN, 1);
			chipset_force_shutdown();
			return POWER_S3;
		}

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * Throttle CPU if necessary.  This should only be asserted
		 * when +VCCP is powered (it is by now).
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, throttle_cpu);

		/* Set PCH_PWROK */
		gpio_set_level(GPIO_PCH_PWROK, 1);

		/* Wait for VCORE_PGOOD before enabling SYS_PWROK */
		if (power_wait_signals(IN_PGOOD_VCORE)) {
			hook_notify(HOOK_CHIPSET_SUSPEND);
			enable_sleep(SLEEP_MASK_AP_RUN);
			gpio_set_level(GPIO_PCH_PWROK, 0);
			gpio_set_level(GPIO_CPU_PROCHOT, 0);
			gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
			gpio_set_level(GPIO_PP3300_DSW_GATED_EN, 1);
			wireless_set_state(WIRELESS_OFF);
			chipset_force_shutdown();
			return POWER_S3;
		}

		/*
		 * Wait a bit for all voltages to be good. PCIe devices need
		 * 99ms, but mini-PCIe devices only need 1ms. Intel recommends
		 * at least 5ms between ALL_SYS_PWRGD and SYS_PWROK.
		 */
		msleep(5);

		/* Set SYS_PWROK */
		gpio_set_level(GPIO_SYS_PWROK, 1);
		return POWER_S0;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/* Clear PCH_PWROK */
		gpio_set_level(GPIO_SYS_PWROK, 0);
		gpio_set_level(GPIO_PCH_PWROK, 0);

		/* Wait 40ns */
		udelay(1);

		/* Suspend wireless */
		wireless_set_state(WIRELESS_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		/* Put touchscreen in reset */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);

		/*
		 * Deassert prochot since CPU is off and we're about to drop
		 * +VCCP.
		 */
		gpio_set_level(GPIO_CPU_PROCHOT, 0);

		/* Turn off DSW gated */
		gpio_set_level(GPIO_PP3300_DSW_GATED_EN, 0);

		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		/* Disable wireless */
		wireless_set_state(WIRELESS_OFF);

		/* Disable peripheral power */
		gpio_set_level(GPIO_ENABLE_TOUCHPAD, 0);
		gpio_set_level(GPIO_PP5000_USB_EN, 0);

		/* Turn off power to RAM */
		gpio_set_level(GPIO_PP1800_EN, 0);
		gpio_set_level(GPIO_PP1200_EN, 0);

		/*
		 * Put touchscreen and lightbar in reset, so we won't
		 * leak +3VALW through the reset line to chips powered
		 * by +5VALW.
		 *
		 * (Note that we're no longer powering down +5VALW due
		 * to crosbug.com/p/16600, but to minimize side effects
		 * of that change we'll still reset these components in
		 * S5.)
		 */
		gpio_set_level(GPIO_TOUCHSCREEN_RESET_L, 0);
		gpio_set_level(GPIO_LIGHTBAR_RESET_L, 0);

		return pause_in_s5 ? POWER_S5 : POWER_S5G3;

	case POWER_S5G3:
		/* Deassert DPWROK */
		gpio_set_level(GPIO_PCH_DPWROK, 0);

		/* Assert RSMRST# */
		gpio_set_level(GPIO_PCH_RSMRST_L, 0);

		/* Turn off power rails enabled in S5 */
		gpio_set_level(GPIO_PP1050_EN, 0);

		/* Check if we can disable PP5000 */
		if (!pp5000_in_g3)
			gpio_set_level(GPIO_PP5000_EN, 0);

		/* Disable 3.3V DSW */
		gpio_set_level(GPIO_PP3300_DSW_EN, 0);
		return POWER_G3;
	}

	return state;
}

/**
 * Set PP5000 rail in G3. The mask represents the reason for
 * turning on/off the PP5000 rail in G3, and enable either
 * enables or disables that mask. If any bit is enabled, then
 * the PP5000 rail will remain on. If all bits are cleared,
 * the rail will turn off.
 *
 * @param mask   Mask to modify
 * @param enable Enable flag
 */
void set_pp5000_in_g3(int mask, int enable)
{
	if (enable)
		atomic_or(&pp5000_in_g3, mask);
	else
		atomic_clear(&pp5000_in_g3, mask);

	/* if we are in G3 now, then set the rail accordingly */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		gpio_set_level(GPIO_PP5000_EN, !!pp5000_in_g3);
}

#ifdef CONFIG_LIGHTBAR_POWER_RAILS
/* Returns true if a change was made, NOT the new state */
int lb_power(int enabled)
{
	int ret = 0;
	int pp5000_en = gpio_get_level(GPIO_PP5000_EN);

	set_pp5000_in_g3(PP5000_IN_G3_LIGHTBAR, enabled);

	/* If the AP is on, we don't change the rails. */
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return ret;

	/* Check if PP5000 rail changed */
	if (gpio_get_level(GPIO_PP5000_EN) != pp5000_en)
		ret = 1;

	/*
	 * When turning on, we have to wait for the rails to come up
	 * fully before we the lightbar ICs will respond. There's not
	 * a reliable PGOOD signal for that (I tried), so we just
	 * have to wait. These delays seem to work.
	 *
	 * Note, we should delay even if the PP5000 rail was already
	 * enabled because we can't be sure it's been enabled long
	 * enough for lightbar IC to respond.
	 */
	if (enabled)
		msleep(10);

	if (enabled != gpio_get_level(GPIO_LIGHTBAR_RESET_L)) {
		ret = 1;
		gpio_set_level(GPIO_LIGHTBAR_RESET_L, enabled);
		msleep(1);
	}

	return ret;
}
#endif

static int host_command_gsv(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_set_value *p = args->params;
	struct ec_response_get_set_value *r = args->response;

	if (p->flags & EC_GSV_SET)
		pause_in_s5 = p->value;

	r->value = pause_in_s5;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GSV_PAUSE_IN_S5,
		     host_command_gsv,
		     EC_VER_MASK(0));

static int console_command_gsv(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &pause_in_s5))
		return EC_ERROR_INVAL;

	ccprintf("pause_in_s5 = %s\n", pause_in_s5 ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pause_in_s5, console_command_gsv,
			"[on|off]",
			"Should the AP pause in S5 during shutdown?",
			NULL);
