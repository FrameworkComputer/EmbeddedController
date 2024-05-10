/* Copyright 2020 The ChromiumOS Authors
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

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/*
 * Define voltage thresholds for SBU USB detection.
 *
 * Max observed USB low across sampled systems: 666mV
 * Min observed USB high across sampled systems: 3026mV
 */
#define GND_MAX_MV 700
#define USB_HIGH_MV 2500
#define SBU_DIRECT 0
#define SBU_FLIP 1

#define MODE_SBU_DISCONNECT 0
#define MODE_SBU_CONNECT 1
#define MODE_SBU_FLIP 2
#define MODE_SBU_OTHER 3

static void ccd_measure_sbu(void);
DECLARE_DEFERRED(ccd_measure_sbu);
static void ccd_measure_sbu(void)
{
	int sbu1;
	int sbu2;
	int mux_en;
	static int count /* = 0 */;
	static int last /* = 0 */;
	static int polarity /* = 0 */;

	/* Read sbu voltage levels */
	sbu1 = adc_read_channel(ADC_SBU1_DET);
	sbu2 = adc_read_channel(ADC_SBU2_DET);
	mux_en = gpio_get_level(GPIO_SBU_MUX_EN);

	/*
	 * While SBU_MUX is disabled (SuzyQ unplugged), we'll poll the SBU lines
	 * to check if an idling, unconfigured USB device is present.
	 * USB FS pulls one line high for connect request.
	 * If so, and it persists for 500ms, we'll enable the SuzyQ in that
	 * orientation.
	 */
	if ((!mux_en) && (sbu1 > USB_HIGH_MV) && (sbu2 < GND_MAX_MV)) {
		/* Check flip connection polarity. */
		if (last != MODE_SBU_FLIP) {
			last = MODE_SBU_FLIP;
			polarity = SBU_FLIP;
			count = 0;
		} else {
			count++;
		}
	} else if ((!mux_en) && (sbu2 > USB_HIGH_MV) && (sbu1 < GND_MAX_MV)) {
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
		 * for 500ms. Since USB is differential, we should never see
		 * GND/GND while the device is connected.
		 * If disconnected, electrically remove SuzyQ.
		 */
	} else if ((mux_en) && (sbu1 < GND_MAX_MV) && (sbu2 < GND_MAX_MV)) {
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
	if (count > 5) {
		if (mux_en) {
			/* Disable mux as it's disconnected now. */
			gpio_set_level(GPIO_SBU_MUX_EN, 0);
			crec_msleep(10);
			CPRINTS("CCD: disconnected.");
		} else {
			/* SBU flip = polarity */
			sbu_flip_sel(polarity);
			gpio_set_level(GPIO_SBU_MUX_EN, 1);
			crec_msleep(10);
			CPRINTS("CCD: connected %s",
				polarity ? "flip" : "noflip");
		}
	}

	/* Measure every 100ms, forever. */
	hook_call_deferred(&ccd_measure_sbu_data, 100 * MSEC);
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
