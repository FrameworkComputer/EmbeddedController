/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/espi.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

#include "signal_vw.h"

#define MY_COMPAT	intel_ap_pwrseq_vw

#if HAS_VW_SIGNALS

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

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

static void espi_handler(const struct device *dev,
			 struct espi_callback *cb,
			 struct espi_event event)
{
	LOG_DBG("ESPI event type 0x%x %d:%d", event.evt_type,
		event.evt_details, event.evt_data);
	switch (event.evt_type) {
	default:
		__ASSERT(0, "ESPI unknown event type: %d",
			 event.evt_type);
		break;

	case ESPI_BUS_RESET:
		/*
		 * Notify that the bus isn't ready, and clear
		 * the signal mask.
		 */
		notify_espi_ready(false);
		espi_ready = 0;
		break;

	case ESPI_BUS_EVENT_VWIRE_RECEIVED:
		for (int i = 0; i < ARRAY_SIZE(vw_config); i++) {
			if (event.evt_details == vw_config[i].espi_signal) {
				signal_data[i] = !!event.evt_data;
				espi_ready |= BIT(i);
			}
		}
		/*
		 * When all the signals have been updated, notify that
		 * the ESPI signals are valid.
		 */
		if (espi_ready == BIT_MASK(ARRAY_SIZE(vw_config))) {
			LOG_DBG("ESPI signals valid");
			/*
			 * TODO(b/222946923): Convert to generalised
			 * callback pattern.
			 */
			notify_espi_ready(true);
		}
		break;
	}
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
	static struct espi_callback espi_cb;

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
	espi_init_callback(&espi_cb, espi_handler,
			   ESPI_BUS_RESET |
			   ESPI_BUS_EVENT_VWIRE_RECEIVED);
	espi_add_callback(espi_dev, &espi_cb);
}

#endif /* HAS_VW_SIGNALS */
