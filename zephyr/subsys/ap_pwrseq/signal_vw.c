/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/espi.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

#include "signal_vw.h"

#define MY_COMPAT	intel_ap_pwrseq_vw

#if HAS_VW_SIGNALS

LOG_MODULE_DECLARE(ap_pwrseq, 3);

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

#define espi_dev DEVICE_DT_GET(DT_CHOSEN(intel_ap_pwrseq_espi))

struct espi_callback espi_bus_cb;
struct espi_callback espi_chan_cb;
struct espi_callback espi_vw_cb;

uint8_t vw_get_level(enum espi_vwire_signal signal)
{
	uint8_t level;

	if (espi_receive_vwire(espi_dev, signal, &level)) {
		LOG_DBG("Espi: Failed to the espi GPIO level\n");
		return 0;
	}

	LOG_DBG("Espi: GPIO level = %d\n", level);
	return level;
}

static void espi_bus_vw_handler(const struct device *dev,
				struct espi_callback *cb,
				struct espi_event event)
{
	LOG_DBG("VW is triggered, event=%d, val=%d\n", event.evt_details,
			vw_get_level(event.evt_details));

	switch (event.evt_details) {
#ifdef CONFIG_PLATFORM_EC_ESPI_VW_SLP_S3
	case ESPI_VWIRE_SIGNAL_SLP_S3:
#endif
#ifdef CONFIG_PLATFORM_EC_ESPI_VW_SLP_S4
	case ESPI_VWIRE_SIGNAL_SLP_S4:
#endif
#ifdef CONFIG_PLATFORM_EC_ESPI_VW_SLP_S5
	case ESPI_VWIRE_SIGNAL_SLP_S5:
#endif
		power_update_signals();
		break;
	default:
		break;
	}
}

/* This should be overridden by the chipset */
__attribute__((weak)) void espi_bus_reset(void)
{
  /* Do nothing */
}

static void espi_bus_reset_handler(const struct device *dev,
				struct espi_callback *cb,
				struct espi_event event)
{
	LOG_DBG("ESPI bus reset");
	espi_bus_reset();
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
	value = vw_get_level(vw_config[vw].espi_signal);
	return vw_config[vw].invert ? !value : value;
}

void power_signal_vw_init(void)
{
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
}

#endif /* HAS_VW_SIGNALS */
