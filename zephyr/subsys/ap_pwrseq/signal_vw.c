/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/espi.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

#include "signal_vw.h"

#define MY_COMPAT	intel_ap_pwrseq_vw

#if HAS_VW_SIGNALS

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define INIT_ESPI_SIGNAL(id)					\
{								\
	.espi_signal = DT_STRING_UPPER_TOKEN(id, virtual_wire),	\
	.invert = DT_PROP(id, vw_invert),			\
},

/*
 * Struct containing the eSPI virtual wire config.
 */
struct vw_config {
	uint8_t espi_signal;	/* associated VW signal */
	bool invert;		/* Invert the signal value */
};

const static struct vw_config vw_config[] = {
DT_FOREACH_STATUS_OKAY(MY_COMPAT, INIT_ESPI_SIGNAL)
};

static bool signal_data[ARRAY_SIZE(vw_config)];

#define espi_dev DEVICE_DT_GET(DT_CHOSEN(intel_ap_pwrseq_espi))

/*
 * Mask of updated signals. If the bus is reset, this is cleared,
 * and it is only when all the signals have been updated that
 * notification is sent that the signals are ready.
 */
static uint8_t espi_ready;
BUILD_ASSERT(ARRAY_SIZE(vw_config) <= 8);

static void espi_bus_vw_handler(const struct device *dev,
				struct espi_callback *cb,
				struct espi_event event)
{
	LOG_DBG("VW is triggered, event=%d, val=%d\n", event.evt_details,
			event.evt_data);

	for (int i = 0; i < ARRAY_SIZE(vw_config); i++) {
		if (event.evt_details == vw_config[i].espi_signal) {
			signal_data[i] = !!event.evt_data;
			espi_ready |= BIT(i);
		}
	}
	/*
	 * When all the signals have been updated, notify that the ESPI
	 * signals are ready.
	 */
	if (espi_ready == BIT_MASK(ARRAY_SIZE(vw_config))) {
		LOG_DBG("ESPI signals ready");
		notify_espi_ready(true);
	}
}

static void espi_bus_reset_handler(const struct device *dev,
				struct espi_callback *cb,
				struct espi_event event)
{
	LOG_DBG("ESPI bus reset");
	/*
	 * Notify that the bus isn't ready, and clear
	 * the signal mask.
	 */
	notify_espi_ready(false);
	espi_ready = 0;
}

static void espi_bus_channel_handler(const struct device *dev,
				struct espi_callback *cb,
				struct espi_event event)
{
	LOG_DBG("ESPI channel ready");
}

int power_signal_vw_get(enum pwr_sig_vw vw)
{
	int value;

	if (vw < 0 || vw >= ARRAY_SIZE(vw_config)) {
		return -EINVAL;
	}
	value = signal_data[vw];
	return vw_config[vw].invert ? !value : value;
}

void power_signal_vw_init(void)
{
	static struct espi_callback espi_bus_cb;
	static struct espi_callback espi_chan_cb;
	static struct espi_callback espi_vw_cb;

	struct espi_cfg cfg = {
		.io_caps = ESPI_IO_MODE_SINGLE_LINE,
		.channel_caps = ESPI_CHANNEL_VWIRE |
			ESPI_CHANNEL_PERIPHERAL |
			ESPI_CHANNEL_OOB,
		/* ESPI_FREQ_MHZ */
		.max_freq = DT_INST_PROP(0, pwrseq_espi_max_freq),
	};

	if (!device_is_ready(espi_dev))	{
		LOG_ERR("Espi device is not ready");
		return;
	}

	if (espi_config(espi_dev, &cfg)) {
		LOG_ERR("Failed to configure eSPI");
		return;
	}

	/* Configure handler for eSPI events */
	espi_init_callback(&espi_bus_cb, espi_bus_reset_handler,
						ESPI_BUS_RESET);
	espi_add_callback(espi_dev, &espi_bus_cb);

	espi_init_callback(&espi_chan_cb, espi_bus_channel_handler,
				ESPI_BUS_EVENT_CHANNEL_READY);
	espi_add_callback(espi_dev, &espi_chan_cb);

	espi_init_callback(&espi_vw_cb, espi_bus_vw_handler,
				ESPI_BUS_EVENT_VWIRE_RECEIVED);
	espi_add_callback(espi_dev, &espi_vw_cb);
	LOG_INF("eSPI initialised");
}

#endif /* HAS_VW_SIGNALS */
