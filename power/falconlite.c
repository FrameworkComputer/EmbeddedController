/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FalconLite chipset power control module for Chrome EC */

#include "builtin/assert.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) gpio_set_level(signal, value)
#endif

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHIPSET, outstr)
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Long power key press to force shutdown in S0. go/crosdebug */
#define FORCED_SHUTDOWN_DELAY (8 * SECOND)
/* Long power key press to boot from S5/G3 state. */
#define POWERBTN_BOOT_DELAY (10 * MSEC)

#define SYS_RST_PULSE_LENGTH (30 * MSEC)

/* Masks for power signals */
#define IN_PG_S5 POWER_SIGNAL_MASK(FCL_PG_S5)
#define IN_PGOOD                                  \
	(POWER_SIGNAL_MASK(FCL_PG_VDD1_VDD2) |    \
	 POWER_SIGNAL_MASK(FCL_PG_VDD_MEDIA_ML) | \
	 POWER_SIGNAL_MASK(FCL_PG_VDD_SOC) |      \
	 POWER_SIGNAL_MASK(FCL_PG_VDD_DDR_OD) | POWER_SIGNAL_MASK(FCL_PG_S5))

#define IN_ALL_S0 IN_PGOOD
#define IN_ALL_S3 IN_PGOOD

/* Power signal list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[FCL_AP_WARM_RST_REQ] = { GPIO_AP_EC_WARM_RST_REQ,
				  POWER_SIGNAL_ACTIVE_HIGH, "AP_WARM_RST_REQ" },
	[FCL_AP_SHUTDOWN_REQ] = { GPIO_AP_EC_SHUTDOWN_REQ_L,
				  POWER_SIGNAL_ACTIVE_LOW, "AP_SHUTDOWN_REQ" },
	[FCL_AP_WATCHDOG] = { GPIO_AP_EC_WATCHDOG_L, POWER_SIGNAL_ACTIVE_LOW,
			      "AP_WDT" },
	[FCL_PG_S5] = { GPIO_PG_S5_PWR_OD, POWER_SIGNAL_ACTIVE_HIGH, "PG_S5" },
	[FCL_PG_VDD1_VDD2] = { GPIO_PG_VDD1_VDD2_OD, POWER_SIGNAL_ACTIVE_HIGH,
			       "PG_VDD1_VDD2" },
	[FCL_PG_VDD_MEDIA_ML] = { GPIO_PG_VDD_MEDIA_ML_OD,
				  POWER_SIGNAL_ACTIVE_HIGH, "PG_VDD_MEDIA_ML" },
	[FCL_PG_VDD_SOC] = { GPIO_PG_VDD_SOC_OD, POWER_SIGNAL_ACTIVE_HIGH,
			     "PG_VDD_SOC" },
	[FCL_PG_VDD_DDR_OD] = { GPIO_PG_VDD_DDR_OD, POWER_SIGNAL_ACTIVE_HIGH,
				"PG_VDD_DDR" },
};

/* Data structure for a GPIO operation for power sequencing */
struct power_seq_op {
	enum gpio_signal signal;
	uint8_t level;
	/* Number of milliseconds to delay after setting signal to level */
	uint32_t delay;
};

/*
 * The entries in the table are handled sequentially from the top
 * to the bottom.
 */

/* The power sequence for POWER_S3S5 */
static const struct power_seq_op s3s5_power_seq[] = {
	{ GPIO_EN_VDD_CPU, 0, 0 },	{ GPIO_EN_VDD_GPU, 0, 0 },
	{ GPIO_EN_VDD_MEDIA_ML, 0, 4 },

	{ GPIO_EN_VDDQ_VR_D, 0, 4 }, /* LPDDR */

	{ GPIO_EN_VDD1_VDD2_VR, 0, 4 }, /* LPDDR */

	{ GPIO_EN_VDD_DDR, 0, 4 },

	{ GPIO_EN_PP3300A_IO_X, 0, 0 }, { GPIO_EN_PP3300_S3, 0, 4 },

	{ GPIO_EN_PP1820A_IO_X, 0, 0 }, { GPIO_EN_PP1800_S3, 0, 0 },
};

/* The power sequence for POWER_G3S5 */
static const struct power_seq_op g3s5_power_seq[] = {
	/* delay 10ms as PP1800_S5 uses PP1800_S5 as alaternative supply */
	{ GPIO_EN_PP5000_S5, 1, 10 },

	{ GPIO_EN_PP1800_S5, 1, 0 },

	{ GPIO_EN_PP1800_VDDIO_PMC_X, 1, 4 },

	{ GPIO_EN_PP0800_VDD_PMC_X, 1, 0 },   { GPIO_EN_VDD_SOC, 1, 4 },

	{ GPIO_EN_PP1800_VDD33_PMC_X, 1, 0 },
};

/* This is the power sequence for POWER_S5S3. */
static const struct power_seq_op s5s3_power_seq[] = {
	{ GPIO_EN_PP1800_S3, 1, 0 },	{ GPIO_EN_PP1820A_IO_X, 1, 4 },

	{ GPIO_EN_PP3300_S3, 1, 0 },	{ GPIO_EN_PP3300A_IO_X, 1, 4 },

	{ GPIO_EN_VDD_DDR, 1, 4 },

	{ GPIO_EN_VDD1_VDD2_VR, 1, 4 }, /* LPDDR */

	{ GPIO_EN_VDDQ_VR_D, 1, 4 }, /* LPDDR */

	{ GPIO_EN_VDD_MEDIA_ML, 1, 0 }, { GPIO_EN_VDD_GPU, 1, 0 },
	{ GPIO_EN_VDD_CPU, 1, 0 },
};

/* The power sequence for POWER_S5G3 */
static const struct power_seq_op s5g3_power_seq[] = {
	{ GPIO_EN_PP1800_VDD33_PMC_X, 0, 4 },

	{ GPIO_EN_VDD_SOC, 0, 0 },

	{ GPIO_EN_PP0800_VDD_PMC_X, 0, 4 },

	{ GPIO_EN_PP1800_VDDIO_PMC_X, 0, 4 },

	{ GPIO_EN_PP1800_S5, 0, 4 },

	{ GPIO_EN_PP5000_S5, 0, 4 },
};

/* most recently received sleep event */
static enum host_sleep_event ap_sleep_event;
/* indicator for shutdown AP */
static char ap_shutdown;
/* indicator for boot AP from off state */
static char boot_from_off;

static void reset_request_interrupt_deferred(void)
{
	chipset_reset(CHIPSET_RESET_AP_REQ);
}
DECLARE_DEFERRED(reset_request_interrupt_deferred);

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	/*
	 * Force power off. This condition will reset once the state machine
	 * transitions to G3.
	 */
	ap_shutdown = 1;
	task_wake(TASK_ID_CHIPSET);
}

void chipset_force_shutdown_button(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_BUTTON);
}
DECLARE_DEFERRED(chipset_force_shutdown_button);

void chipset_exit_hard_off_button(void)
{
	/* Power up from off */
	ap_shutdown = 0;
	boot_from_off = 1;
	CPRINTS("PWRON:BTN");
	chipset_exit_hard_off();
}
DECLARE_DEFERRED(chipset_exit_hard_off_button);

void chipset_reset_request_interrupt(enum gpio_signal signal)
{
	/*
	 * indicator for the following reset is a reboot or a AP requested
	 * shutdown.
	 */
	static char want_reboot;

	if (signal == GPIO_AP_EC_WARM_RST_REQ) {
		CPRINTS("AP wants reboot");
		hook_call_deferred(&reset_request_interrupt_deferred_data, 0);
		want_reboot = 1;
	} else if (signal == GPIO_AP_EC_SHUTDOWN_REQ_L) {
		/*
		 * When AP_SHUTDOWN_REQ_L is asserted, we have to check if
		 * there is a AP_EC_WARM_RST_REQ interrupt prior to this one,
		 * and that would be a reboot request, rather than a
		 * shutdown. In the meantime, the WDT should not be asserted,
		 * or this is a WDT reset, which will be handled by AP.
		 */
		if (gpio_get_level(GPIO_AP_EC_WATCHDOG_L) &&
		    !gpio_get_level(signal) && !want_reboot) {
			CPRINTS("AP wants shutdown");
			ap_shutdown = 1;
		}
		want_reboot = 0;
	}
	power_signal_interrupt(signal);
}

enum power_state power_chipset_init(void)
{
	uint32_t reset_flags = system_get_reset_flags();
	int exit_hard_off = 1;

	/* Enable reboot / sleep control inputs from AP */
	gpio_enable_interrupt(GPIO_AP_EC_WARM_RST_REQ);
	gpio_enable_interrupt(GPIO_AP_EC_SHUTDOWN_REQ_L);

	if (reset_flags & EC_RESET_FLAG_SYSJUMP) {
		if ((power_get_signals() & IN_ALL_S0) == IN_ALL_S0) {
			disable_sleep(SLEEP_MASK_AP_RUN);
			CPRINTS("already in S0");
			return POWER_S0;
		}
	} else if (reset_flags & EC_RESET_FLAG_AP_OFF) {
		exit_hard_off = 0;
	} else if ((reset_flags & EC_RESET_FLAG_HIBERNATE) &&
		   gpio_get_level(GPIO_AC_PRESENT)) {
		/*
		 * If AC present, assume this is a wake-up by AC insert.
		 * Boot EC only.
		 *
		 * Note that extpower module is not initialized at this point,
		 * the only way is to ask GPIO_AC_PRESENT directly.
		 */
		exit_hard_off = 0;
	}

	if (battery_is_present() == BP_YES)
		/*
		 * (crosbug.com/p/28289): Wait battery stable.
		 * Some batteries use clock stretching feature, which requires
		 * more time to be stable.
		 */
		battery_wait_for_stable();

	if (exit_hard_off) {
		CPRINTS("PWRON:0x%x", reset_flags);
		ap_shutdown = 0;
		boot_from_off = 1;
		/* Auto-power on */
		chipset_exit_hard_off();
	}

	/* Start from S5 if the rail is already up. */
	if (power_get_signals() & IN_PG_S5) {
		/* Force shutdown from S5 if the rails is already up. */
		if (!exit_hard_off)
			ap_shutdown = 1;
		return POWER_S5;
	}

	return POWER_G3;
}

void chipset_reset(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s: %d", __func__, reason);
	report_ap_reset(reason);

	GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);
	crec_usleep(SYS_RST_PULSE_LENGTH);
	GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 1);
}
/**
 * Step through the power sequence table and do corresponding GPIO operations.
 *
 * @param	power_seq_ops	The pointer to the power sequence table.
 * @param	op_count	The number of entries of power_seq_ops.
 */
static void power_seq_run(const struct power_seq_op *power_seq_ops,
			  int op_count)
{
	int i;

	for (i = 0; i < op_count; i++) {
		GPIO_SET_LEVEL(power_seq_ops[i].signal, power_seq_ops[i].level);
		if (!power_seq_ops[i].delay)
			continue;
		crec_msleep(power_seq_ops[i].delay);
	}
}

enum power_state power_handle_state(enum power_state state)
{
	/* Retry S5->S3 transition, if not zero. */
	static int s5s3_retry;

	switch (state) {
	case POWER_G3:
		break;

	case POWER_S5:
		if (boot_from_off) {
			s5s3_retry = 1;
			return POWER_S5S3;
		}

		/*
		 * Stay in S5, common code will drop to G3 after timeout
		 * if the long press does not work.
		 */
		return POWER_S5;
	case POWER_S3:
		if (!power_has_signals(IN_PGOOD) || ap_shutdown)
			return POWER_S3S5;
		else if (ap_sleep_event == HOST_SLEEP_EVENT_S3_RESUME ||
			 boot_from_off)
			return POWER_S3S0;
		break;

	case POWER_S0:
		if (ap_sleep_event == HOST_SLEEP_EVENT_S3_SUSPEND ||
		    !power_has_signals(IN_ALL_S0) || ap_shutdown)
			return POWER_S0S3;
		break;

	case POWER_G3S5:
		ap_shutdown = 0;
		power_seq_run(g3s5_power_seq, ARRAY_SIZE(g3s5_power_seq));

		/* Power up to next state, or go back */
		if (power_get_signals() & IN_PG_S5)
			return POWER_S5;
		else
			return POWER_G3;
		break;

	case POWER_S5S3:
		hook_notify(HOOK_CHIPSET_PRE_INIT);

		power_seq_run(s5s3_power_seq, ARRAY_SIZE(s5s3_power_seq));

		/*
		 * Wait for rails up. Retry if it fails
		 * (it may take 2 attempts on restart after we use
		 * force reset).
		 */
		if (!power_has_signals(IN_ALL_S3)) {
			if (s5s3_retry) {
				s5s3_retry = 0;
				return POWER_S5S3;
			}
			boot_from_off = 0;
			/* Give up, go back to G3. */
			return POWER_S5G3;
		}

		GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 1);

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_STARTUP);

		/* Power up to next state */
		return POWER_S3;

	case POWER_S3S5:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN);

		GPIO_SET_LEVEL(GPIO_SYS_RST_ODL, 0);
		power_seq_run(s3s5_power_seq, ARRAY_SIZE(s3s5_power_seq));

		/* Call hooks after we remove power rails */
		hook_notify(HOOK_CHIPSET_SHUTDOWN_COMPLETE);

		/* Start shutting down */
		return POWER_S5;

	case POWER_S0S3:
		/* Call hooks before we remove power rails */
		hook_notify(HOOK_CHIPSET_SUSPEND);

		/*
		 * Enable idle task deep sleep. Allow the low power idle task
		 * to go into deep sleep in S3 or lower.
		 */
		enable_sleep(SLEEP_MASK_AP_RUN);

		/*
		 * In case the power button is held awaiting power-off timeout,
		 * power off immediately now that we're entering S3.
		 */
		if (power_button_is_pressed()) {
			ap_shutdown = 1;
			hook_call_deferred(&chipset_force_shutdown_button_data,
					   -1);
		}

		return POWER_S3;

	case POWER_S3S0:
		if (power_wait_signals(IN_ALL_S0)) {
			chipset_force_shutdown(CHIPSET_SHUTDOWN_WAIT);
			return POWER_S0S3;
		}
		boot_from_off = 0;

		/* Call hooks now that rails are up */
		hook_notify(HOOK_CHIPSET_RESUME);

		/*
		 * Disable idle task deep sleep. This means that the low
		 * power idle task will not go into deep sleep while in S0.
		 */
		disable_sleep(SLEEP_MASK_AP_RUN);

		/* Power up to next state */
		return POWER_S0;

	case POWER_S5G3:
		power_seq_run(s5g3_power_seq, ARRAY_SIZE(s5g3_power_seq));
		return POWER_G3;

	default:
		CPRINTS("Unexpected power state %d", state);
		ASSERT(0);
		break;
	}

	return state;
}

static void power_button_changed(void)
{
	if (power_button_is_pressed()) {
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			hook_call_deferred(&chipset_exit_hard_off_button_data,
					   POWERBTN_BOOT_DELAY);

		/* Delayed power down from S0/S3, cancel on PB release */
		hook_call_deferred(&chipset_force_shutdown_button_data,
				   FORCED_SHUTDOWN_DELAY);
	} else {
		/* Power button released, cancel deferred shutdown/boot */
		hook_call_deferred(&chipset_exit_hard_off_button_data, -1);
		hook_call_deferred(&chipset_force_shutdown_button_data, -1);
	}
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_changed, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
__override void
power_chipset_handle_host_sleep_event(enum host_sleep_event state,
				      struct host_sleep_event_context *ctx)
{
	CPRINTS("Handle sleep: %d", state);

	ap_sleep_event = state;

	if (state == HOST_SLEEP_EVENT_S3_RESUME ||
	    state == HOST_SLEEP_EVENT_S3_SUSPEND)
		task_wake(TASK_ID_CHIPSET);
}
#endif /* CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

#ifdef CONFIG_LID_SWITCH
static void lid_changed(void)
{
	/* Power-up from off on lid open */
	if (lid_is_open() && chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		CPRINTS("PWRON:LIDOPEN");
		ap_shutdown = 0;
		boot_from_off = 1;
		chipset_exit_hard_off();
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_changed, HOOK_PRIO_DEFAULT);
#endif
