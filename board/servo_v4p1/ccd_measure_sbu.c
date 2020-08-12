/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* CCD Measure SBU */

#include "adc.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpanders.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/*
 * Define voltage thresholds for SBU USB detection.
 *
 * Max observed USB low across sampled systems: 666mV
 * Min observed USB high across sampled systems: 3026mV
 */
#define GND_MAX_MV      700
#define USB_HIGH_MV     2500
#define SBU_DIRECT      0
#define SBU_FLIP        1

#define MODE_SBU_DISCONNECT     0
#define MODE_SBU_CONNECT        1
#define MODE_SBU_FLIP           2
#define MODE_SBU_OTHER          3

static void ccd_measure_sbu(void);
DECLARE_DEFERRED(ccd_measure_sbu);
static void ccd_measure_sbu(void)
{
	int sbu1;
	int sbu2;
	static int count /* = 0 */;
	static int last /* = 0 */;
	static int polarity /* = 0 */;

	/*
	 * The SBU mux should be enabled so SBU is connected to the USB2
	 * hub. The hub needs to apply its terminations to read the
	 * correct SBU levels.
	 */
	gpio_set_level(GPIO_SBU_MUX_EN, 1);

	/* Read sbu voltage levels */
	sbu1 = adc_read_channel(ADC_SBU1_DET);
	sbu2 = adc_read_channel(ADC_SBU2_DET);

	/*
	 * Poll the SBU lines to check if an idling, unconfigured USB device is
	 * present. USB FS pulls one line high for connect request. If so, and
	 * it persists for 500ms, we'll enable the SuzyQ in that orientation.
	 */
	if ((sbu1 > USB_HIGH_MV) && (sbu2 < GND_MAX_MV)) {
		/* Check flip connection polarity. */
		if (last != MODE_SBU_FLIP) {
			last = MODE_SBU_FLIP;
			polarity = SBU_FLIP;
			count = 0;
		} else {
			count++;
		}
	} else if ((sbu2 > USB_HIGH_MV) && (sbu1 < GND_MAX_MV)) {
		/* Check direct connection polarity. */
		if (last != MODE_SBU_CONNECT) {
			last = MODE_SBU_CONNECT;
			polarity = SBU_DIRECT;
			count = 0;
		} else {
			count++;
		}
	/*
	 * If SuzyQ is enabled, we'll poll for a persistent no-signal
	 * for 500ms. We may see GND/GND while passing data since the
	 * ADC's don't sample simultaneously, so there needs to be a
	 * deglitch to not falsely detect a disconnect. If disconnected,
	 * take no action. We need to keep the mux enabled to detect
	 * another device.
	 */
	} else if ((sbu1 < GND_MAX_MV) && (sbu2 < GND_MAX_MV)) {
		/* Check for SBU disconnect if connected. */
		if (last != MODE_SBU_DISCONNECT) {
			last = MODE_SBU_DISCONNECT;
			count = 0;
		} else {
			count++;
		}
	} else {
		/* Didn't find anything, reset state. */
		last = MODE_SBU_OTHER;
		count = 0;
	}

	/*
	 * We have seen a new state continuously for 500ms.
	 * Let's update the mux to enable/disable SuzyQ appropriately.
	 */
	if (count == 50) {
		if (last == MODE_SBU_DISCONNECT) {
			CPRINTS("CCD: disconnected.");
		} else {
			/* SBU flip = polarity */
			sbu_flip_sel(polarity);
			CPRINTS("CCD: connected %s",
				polarity ? "flip" : "noflip");
		}
	}

	/* Measure every 10ms, forever. */
	hook_call_deferred(&ccd_measure_sbu_data, 10 * MSEC);
}

void ccd_enable(int enable)
{
	if (enable) {
		hook_call_deferred(&ccd_measure_sbu_data, 0);
	} else {
		gpio_set_level(GPIO_SBU_MUX_EN, 0);
		hook_call_deferred(&ccd_measure_sbu_data, -1);
	}
}

void start_ccd_meas_sbu_cycle(void)
{
	hook_call_deferred(&ccd_measure_sbu_data, 1000 * MSEC);
}
