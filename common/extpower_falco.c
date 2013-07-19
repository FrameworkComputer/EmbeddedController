/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Falco adapters can support "charger hybrid turbo boost" mode and other
 * buzzwords. The limits vary depending on each adapter's power rating, so we
 * need to watch for changes and adjust the limits and high-current thresholds
 * accordingly. If we go over, the AP needs to throttle itself. The EC's
 * charging state logic isn't affected, just the AP's P-State. We try to save
 * PROCHOT as a last resort.
 */

#include <limits.h>				/* part of the compiler */

#include "adc.h"
#include "charge_state.h"
#include "charger.h"
#include "charger_bq24738.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "extpower_falco.h"
#include "hooks.h"
#include "host_command.h"
#include "smart_battery.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* Values for our supported adapters */
static const char * const ad_name[] = {
	"unknown",
	"45W",
	"65W",
	"90W"
};
BUILD_ASSERT(ARRAY_SIZE(ad_name) == NUM_ADAPTER_TYPES);

test_export_static
struct adapter_id_vals ad_id_vals[] = {
	/* mV low, mV high */
	{INT_MIN, INT_MAX},			/* anything = ADAPTER_UNKNOWN */
	{434,     554},				/* ADAPTER_45W */
	{561,     717},				/* ADAPTER_65W */
	{725,     925}				/* ADAPTER_90W */
};
BUILD_ASSERT(ARRAY_SIZE(ad_id_vals) == NUM_ADAPTER_TYPES);

test_export_static
int ad_input_current[][NUM_AC_TURBO_STATES] = {
	/* Current limits in mA, for turbo off and turbo on. In hex,
	 * because the BQ24738 Input Current Register masks off bits 6-0.
	 * FIXME: That constraint may vary with other chargers. */
	{0x0a00, 0x0a00},			/* ADAPTER_UNKNOWN */
	{0x0a00, 0x0800},			/* ADAPTER_45W */
	{0x0a00, 0x0c00},			/* ADAPTER_65W */
	{0x0f00, 0x1100}			/* ADAPTER_90W */
};
BUILD_ASSERT(ARRAY_SIZE(ad_input_current) == NUM_ADAPTER_TYPES);

test_export_static
struct adapter_limits ad_limits[][NUM_AC_TURBO_STATES][NUM_AC_THRESHOLDS] = {
	/* ADAPTER_UNKNOWN - treat as 65W, no turbo */
	{
		/* Turbo off */
		{
			{ 3080, 2730, 16, 80, },
			{ 3280, 2930, 1, 80, },
		},
		/* Turbo on - unused, except for testing */
		{
			{ 3080, 2730, 16, 80, },
			{ 3280, 2930, 1, 80, },
		}
	},
	/* ADAPTER_45W */
	{
		/* Turbo off */
		{
			{ 2050, 1700, 16, 80, },
			{ 2260, 1910, 1, 80, },
		},
		/* Turbo on */
		{
			{ 2310, 1960, 16, 80, },
			{ 2560, 2210, 1, 80, },
		}
	},
	/* ADAPTER_65W */
	{
		/* Turbo off */
		{
			{ 3080, 2730, 16, 80, },
			{ 3280, 2930, 1, 80, },
		},
		/* Turbo on */
		{
			{ 3330, 2980, 16, 80, },
			{ 3590, 3240, 1, 80, },
		}
	},
	/* ADAPTER_90W */
	{
		/* Turbo off */
		{
			{ 4360, 4010, 16, 80, },
			{ 4560, 4210, 1, 80, },
		},
		/* Turbo on */
		{
			{ 4620, 4270, 16, 80, },
			{ 4870, 4520, 1, 80, },
		}
	}
};
BUILD_ASSERT(ARRAY_SIZE(ad_limits) == NUM_ADAPTER_TYPES);


static int last_mv;
static enum adapter_type identify_adapter(void)
{
	int i;
	last_mv = adc_read_channel(ADC_AC_ADAPTER_ID_VOLTAGE);

	/* ADAPTER_UNKNOWN matches everything, so search backwards */
	for (i = NUM_ADAPTER_TYPES - 1; i >= 0; i--)
		if (last_mv >= ad_id_vals[i].lo && last_mv <= ad_id_vals[i].hi)
			return i;

	return ADAPTER_UNKNOWN;			/* should never get here */
}

test_export_static enum adapter_type ac_adapter;
static void ac_change_callback(void)
{
	if (extpower_is_present()) {
		ac_adapter = identify_adapter();
		CPRINTF("[%T AC Adapter is %s (%dmv)]\n",
			ad_name[ac_adapter], last_mv);
	} else {
		ac_adapter = ADAPTER_UNKNOWN;
		CPRINTF("[%T AC Adapter is not present]\n");
		/* Charger unavailable. Clear local flags */
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_change_callback, HOOK_PRIO_DEFAULT);

test_export_static int ac_turbo = -1;
static void set_turbo(int on)
{
	int tmp, r;

	if (ac_turbo == on)
		return;

	CPRINTF("[%T turbo mode => %d]\n", on);

	/* Set/clear turbo mode in charger */
	r = charger_get_option(&tmp);
	if (r != EC_SUCCESS)
		goto bad;

	if (on)
		tmp |= OPTION_BOOST_MODE_ENABLE;
	else
		tmp &= ~OPTION_BOOST_MODE_ENABLE;

	r = charger_set_option(tmp);
	if (r != EC_SUCCESS)
		goto bad;

	/* Set allowed Io based on adapter */
	r = charger_set_input_current(ad_input_current[ac_adapter][on]);
	if (r != EC_SUCCESS)
		goto bad;

	ac_turbo = on;
	return;
bad:
	CPRINTF("[%T ERROR: can't talk to charger: %d]\n", r);
}

/* FIXME: There's already a chipset_throttle_cpu() function. However, it's a
 * fairly large hammer - on x86, it just asserts PROCHOT.  That's less than
 * ideal for the turbo boost charger stuff. We might want to make this
 * function generic enough to use from other places. For now, meh. */
test_export_static int ap_is_throttled;
static void set_throttle(int on)
{
	if (on)
		host_set_events(EC_HOST_EVENT_MASK(
					EC_HOST_EVENT_THROTTLE_START));
	else
		host_set_events(EC_HOST_EVENT_MASK(
					EC_HOST_EVENT_THROTTLE_STOP));
	ap_is_throttled = on;
}

test_export_static
void check_threshold(int current, struct adapter_limits *lim)
{
	if (lim->triggered) {
		/* watching for current to drop */
		if (current < lim->lo_val) {
			if (++lim->count >= lim->lo_cnt) {
				set_throttle(0);
				lim->count = 0;
				lim->triggered = 0;
			}
		} else {
			lim->count = 0;
		}
	} else {
		/* watching for current to rise */
		if (current > lim->hi_val) {
			if (++lim->count >= lim->hi_cnt) {
				set_throttle(1);
				lim->count = 0;
				lim->triggered = 1;
			}
		} else {
			lim->count = 0;
		}
	}
}

void watch_adapter_closely(struct power_state_context *ctx)
{
	int current, i;

	/* We can only talk to the charger if we're on AC. If there are no
	 * errors and we recognize the adapter, enable Turbo at 15% charge,
	 * disable it at 10% to provide hysteresis. */
	if (extpower_is_present()) {
		if (ctx->curr.error ||
		    ctx->curr.batt.state_of_charge < 10 ||
		    ac_adapter == ADAPTER_UNKNOWN) {
			set_turbo(0);
		} else if (ctx->curr.batt.state_of_charge > 15) {
			set_turbo(1);
		}
	}

	/* If the AP is off, we won't need to throttle it. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF |
			     CHIPSET_STATE_SUSPEND))
		return;

	/* And if we're not on AC, we can't monitor the current. */
	if (!extpower_is_present()) {
		ac_turbo = -1;			/* watch for its return */
		return;
	}

	/* Check all the thresholds. */
	current = adc_read_channel(ADC_CH_CHARGER_CURRENT);
	for (i = 0; i < NUM_AC_THRESHOLDS; i++)
		check_threshold(current, &ad_limits[ac_adapter][ac_turbo][i]);
}

static int command_adapter(int argc, char **argv)
{
	enum adapter_type v = identify_adapter();
	ccprintf("Adapter %s (%dmv), turbo %d, AP_throttled %d\n",
		 ad_name[v], last_mv, ac_turbo, ap_is_throttled);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(adapter, command_adapter,
			NULL,
			"Display AC adapter information",
			NULL);
