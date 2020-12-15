/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gl3590.h"
#include "ina231s.h"
#include "ioexpanders.h"
#include "system.h"
#include "usb_tc_snk_sm.h"
#include "util.h"
#include "pwr_defs.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/*
 * Power alert threshold, defines what is the trigger level for used power in
 * relation to all available input power in percentages.
 */
#define PWR_ALERT_TH	(90)

/* Cache previous power in milliwatts in order to detect changes */
static int old_pwr_mw;

static int is_bc12_enabled(void)
{
	return get_host_chrg_det();
}

void evaluate_input_power(void)
{
	struct pwr_con_t host_hub_pwr = {0}, bc12_pwr = {0}, srv_chg_pwr = {0};
	struct pwr_con_t *available_pwr;

	if (gl3590_ufp_pwr(0, &host_hub_pwr)) {
		CPRINTF("Cannot get host connection power data, assuming "
			 "5V/500mA\n");
		host_hub_pwr.volts = 5;
		host_hub_pwr.milli_amps = 500;
	}

	if (is_bc12_enabled()) {
		bc12_pwr.volts = 5;
		bc12_pwr.milli_amps = 1500;
	}

	/*
	 * It is possible that we will get less power from servo charger port
	 * than from the host connection, however the design of power
	 * multiplexer circuit doesn't allow to switch back from alternate
	 * supply. That's why once enabled, servo charger power will be always
	 * used.
	 */
	if (get_alternate_port_pwr(&srv_chg_pwr)) {
		available_pwr = pwr_con_to_milliwatts(&host_hub_pwr) >
				pwr_con_to_milliwatts(&bc12_pwr) ?
					&host_hub_pwr : &bc12_pwr;
	} else {
		available_pwr = &srv_chg_pwr;
	}

	if (pwr_con_to_milliwatts(available_pwr) != old_pwr_mw) {
		CPRINTF("Servo now powered %dV/%dmA\n", available_pwr->volts,
			 available_pwr->milli_amps);
		set_sr_chg_power_limit(pwr_con_to_milliwatts(available_pwr) *
				   PWR_ALERT_TH / 100);
		old_pwr_mw = pwr_con_to_milliwatts(available_pwr);
	}
}
