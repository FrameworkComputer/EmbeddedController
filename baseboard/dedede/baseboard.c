/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dedede family-specific configuration */

#include "adc.h"
#include "board_config.h"
#include "cbi_fw_config.h"
#include "charger/isl923x_public.h"
#include "charger/sm5803.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "power/icelake.h"
#include "power/intel_x86.h"
#include "system.h"
#include "usb_pd.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/******************************************************************************/
/*
 * PWROK signal configuration, see the PWROK Generation Flow Diagram in the
 * Jasper Lake Platform Design Guide for the list of potential signals.
 *
 * Dedede boards use this PWROK sequence:
 *	GPIO_ALL_SYS_PWRGD - turns on VCCIN rail
 *	GPIO_EC_AP_VCCST_PWRGD_OD - asserts VCCST_PWRGD to AP, requires 2ms
 *		delay from VCCST stable to meet the tCPU00 platform sequencing
 *		timing
 *	GPIO_EC_AP_PCH_PWROK_OD - asserts PMC_PCH_PWROK to the AP. Note that
 *		PMC_PCH_PWROK is also gated by the IMVP9_VRRDY_OD output from
 *		the VCCIN voltage rail controller.
 *	GPIO_EC_AP_SYS_PWROK - asserts PMC_SYS_PWROK to the AP
 *
 * Both PMC_PCH_PWROK and PMC_SYS_PWROK signals must both be asserted before
 * the Jasper Lake SoC deasserts PMC_RLTRST_N. The platform may deassert
 * PMC_PCH_PWROK and PMC_SYS_PWROK in any order to optimize overall boot
 * latency.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {
	{
		.gpio = GPIO_ALL_SYS_PWRGD,
	},
	{
		.gpio = GPIO_EC_AP_VCCST_PWRGD_OD,
		.delay_ms = 2,
	},
	{
		.gpio = GPIO_EC_AP_PCH_PWROK_OD,
	},
	{
		.gpio = GPIO_EC_AP_SYS_PWROK,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	/* No delays needed during S0 exit */
	{
		.gpio = GPIO_EC_AP_VCCST_PWRGD_OD,
	},
	{
		.gpio = GPIO_EC_AP_PCH_PWROK_OD,
	},
	{
		.gpio = GPIO_EC_AP_SYS_PWROK,
	},
	/* Turn off the VCCIN rail last */
	{
		.gpio = GPIO_ALL_SYS_PWRGD,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_deassert_list);

/*
 * Dedede does not use hibernate wake pins, but the super low power "Z-state"
 * instead in which the EC is powered off entirely.  Power will be restored to
 * the EC once one of the wake up events occurs.  These events are ACOK, lid
 * open, and a power button press.
 */
const enum gpio_signal hibernate_wake_pins[] = {};
const int hibernate_wake_pins_used;

__override void board_after_rsmrst(int rsmrst)
{
	/*
	 * b:148688874: If RSMRST# is de-asserted, enable the pull-up on
	 * PG_PP1050_ST_OD.  It won't be enabled prior to this signal going high
	 * because the load switch for PP1050_ST cannot pull the PG low.  Once
	 * it's asserted, disable the pull up so we don't inidicate that the
	 * power is good before the rail is actually ready.
	 */
	int flags = rsmrst ? GPIO_PULL_UP : 0;

	flags |= GPIO_INT_BOTH;

	gpio_set_flags(GPIO_PG_PP1050_ST_OD, flags);
}

/*
 * Dedede does not have a GPIO indicating ACOK, therefore the charger or TCPC
 * can call this function once it detects a VBUS presence change with which we
 * can trigger the HOOK_AC_CHANGE hook.
 */
__override void board_check_extpower(void)
{
	static int last_extpower_present;
	int extpower_present = extpower_is_present();

	if (last_extpower_present ^ extpower_present)
		extpower_handle_update(extpower_present);

	last_extpower_present = extpower_present;
}

atomic_t pp3300_a_pgood;
static int get_pg_ec_dsw_pwrok(void)
{
	/*
	 * The PP3300_A rail is an input to generate DPWROK.  Assuming that
	 * power is good if voltage is at least 80% of nominal level.  We cannot
	 * read the ADC values during an interrupt, therefore, this power good
	 * value is updated via ADC threshold interrupts.
	 */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/*
		 * The ADC interrupts are disabled in suspend for PP3000_A,
		 * therefore this value may be stale. Assume that the PGOOD
		 * follows the enable signal for this case only.
		 */
		if (!gpio_get_level(GPIO_EN_PP3300_A)) {
			CPRINTS("EN_PP3300_A is low, assuming PG is low!");
			atomic_clear(&pp3300_a_pgood);
		} else {
			atomic_or(&pp3300_a_pgood, 1);
		}
	}
	return pp3300_a_pgood;
}

/* Store away PP300_A good status before sysjumps */
#define BASEBOARD_SYSJUMP_TAG 0x4242 /* BB */
#define BASEBOARD_HOOK_VERSION 1

static void pp3300_a_pgood_preserve(void)
{
	system_add_jump_tag(BASEBOARD_SYSJUMP_TAG, BASEBOARD_HOOK_VERSION,
			    sizeof(pp3300_a_pgood), &pp3300_a_pgood);
}
DECLARE_HOOK(HOOK_SYSJUMP, pp3300_a_pgood_preserve, HOOK_PRIO_DEFAULT);

static void baseboard_prepare_power_signals(void)
{
	const int *stored;
	int version, size;

	stored = (const int *)system_get_jump_tag(BASEBOARD_SYSJUMP_TAG,
						  &version, &size);
	if (stored && (version == BASEBOARD_HOOK_VERSION) &&
	    (size == sizeof(pp3300_a_pgood)))
		/* Valid PP3300 status found, restore before CHIPSET init */
		pp3300_a_pgood = *stored;

	/* Restore pull-up on PG_PP1050_ST_OD */
	if (system_jumped_to_this_image() &&
	    gpio_get_level(GPIO_PG_EC_RSMRST_ODL))
		board_after_rsmrst(1);
}
DECLARE_HOOK(HOOK_INIT, baseboard_prepare_power_signals, HOOK_PRIO_FIRST);

static int get_pg_ec_all_sys_pwrgd(void)
{
	/*
	 * SLP_S3_L is a qualifying input signal to ALL_SYS_PWRGD logic.
	 * So ensure ALL_SYS_PWRGD remains LOW during SLP_S3_L assertion.
	 */
	if (!gpio_get_level(GPIO_SLP_S3_L))
		return 0;
	/*
	 * ALL_SYS_PWRGD is an AND of DRAM PGOOD, VCCST PGOOD, and VCCIO_EXT
	 * PGOOD.
	 */
	return gpio_get_level(GPIO_PG_PP1050_ST_OD) &&
	       gpio_get_level(GPIO_PG_DRAM_OD) &&
	       gpio_get_level(GPIO_PG_VCCIO_EXT_OD);
}

__override int power_signal_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_PG_EC_DSW_PWROK)
		return get_pg_ec_dsw_pwrok();

	if (signal == GPIO_PG_EC_ALL_SYS_PWRGD)
		return get_pg_ec_all_sys_pwrgd();

	if (IS_ENABLED(CONFIG_HOST_INTERFACE_ESPI)) {
		/* Check signal is from GPIOs or VWs */
		if (espi_signal_is_vw(signal))
			return espi_vw_get_wire((enum espi_vw_signal)signal);
	}
	return gpio_get_level(signal);
}

void baseboard_all_sys_pgood_interrupt(enum gpio_signal signal)
{
	/*
	 * We need to deassert ALL_SYS_PGOOD within 200us of SLP_S3_L asserting.
	 * that is why we do this here instead of waiting for the chipset
	 * driver to.
	 * Early protos do not pull VCCST_PWRGD below Vil in hardware logic,
	 * so we need to do the same for this signal.
	 * Pull EN_VCCIO_EXT to LOW, which ensures VCCST_PWRGD remains LOW
	 * during SLP_S3_L assertion.
	 */
	if (!gpio_get_level(GPIO_SLP_S3_L)) {
		gpio_set_level(GPIO_ALL_SYS_PWRGD, 0);
		gpio_set_level(GPIO_EN_VCCIO_EXT, 0);
		gpio_set_level(GPIO_EC_AP_VCCST_PWRGD_OD, 0);
		gpio_set_level(GPIO_EC_AP_PCH_PWROK_OD, 0);
	}
	/* Now chain off to the normal power signal interrupt handler. */
	power_signal_interrupt(signal);
}

void baseboard_chipset_startup(void)
{
#ifdef CONFIG_PWM_KBLIGHT
	/* Allow keyboard backlight to be enabled */
	gpio_set_level(GPIO_EN_KB_BL, 1);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, baseboard_chipset_startup,
	     HOOK_PRIO_DEFAULT);

void baseboard_chipset_shutdown(void)
{
#ifdef CONFIG_PWM_KBLIGHT
	/* Turn off the keyboard backlight if it's on. */
	gpio_set_level(GPIO_EN_KB_BL, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, baseboard_chipset_shutdown,
	     HOOK_PRIO_DEFAULT);

void board_hibernate_late(void)
{
	volatile uint32_t busy = 0;

	/* Disable any pull-ups on C0 and C1 interrupt lines */
	gpio_set_flags(GPIO_USB_C0_INT_ODL, GPIO_INPUT);
#if CONFIG_USB_PD_PORT_MAX_COUNT > 1
	gpio_set_flags(GPIO_USB_C1_INT_ODL, GPIO_INPUT);
#endif
	/*
	 * Turn on the Z state.  This will not return as it will cut power to
	 * the EC.
	 */
	gpio_set_level(GPIO_EN_SLP_Z, 1);

	/*
	 * Interrupts are disabled at this point, so busy-loop to consume some
	 * time (something on the order of at least 1 second, depending on EC
	 * chip being used)
	 */
	while (busy < 100000)
		busy++;

	/*
	 * Still awake despite turning on zombie state?  Reset with AP off is
	 * the best we can do in this situation.
	 */
	system_reset(SYSTEM_RESET_LEAVE_AP_OFF);

	/* Await our reset */
	while (1)
		;
}

int board_is_i2c_port_powered(int port)
{
	if (port != I2C_PORT_SENSOR)
		return 1;

	/* Sensor rails are off in S5/G3 */
	return chipset_in_state(CHIPSET_STATE_ANY_OFF) ? 0 : 1;
}

#if defined(CONFIG_CHARGER_RAA489000) || defined(CONFIG_CHARGER_SM5803)
__overridable int extpower_is_present(void)
{
	int port;
	int rv;
	bool acok;
	enum ec_error_list (*check_acok)(int port, bool *acok);

	if (IS_ENABLED(CONFIG_CHARGER_RAA489000))
		check_acok = raa489000_is_acok;
	else if (IS_ENABLED(CONFIG_CHARGER_SM5803))
		check_acok = sm5803_is_acok;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		rv = check_acok(port, &acok);
		if ((rv == EC_SUCCESS) && acok)
			return 1;
	}

	return 0;
}
#endif

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	/*
	 * Remove keyboard backlight feature for devices that don't support it.
	 */
	if (get_cbi_fw_config_kblight() == KB_BL_ABSENT)
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
	else
		return flags0;
}
