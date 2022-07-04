/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <soc_espi.h>
#include <ap_power/ap_power.h>
#include <chipset.h>
#include <devicetree.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(ec_chip_it8xxx2_espi, CONFIG_ESPI_LOG_LEVEL);

/*
 * When eSPI CS# is held low, it prevents IT8xxx2 from entering deep doze.
 * To allow deep doze and save power, disable the eSPI inputs while the AP is
 * in G3.
 */
static const struct device *const espi_device =
	DEVICE_DT_GET(DT_NODELABEL(espi0));

static void espi_enable_callback(struct ap_power_ev_callback *cb,
				 struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_INITIALIZED:
		/* When AP power state becomes known, sync eSPI enable */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
			LOG_DBG("AP off; disabling eSPI");
			espi_it8xxx2_enable_pad_ctrl(espi_device, false);
		}
		break;
	case AP_POWER_PRE_INIT:
	case AP_POWER_HARD_OFF: {
		bool enable = data.event == AP_POWER_PRE_INIT;

		LOG_DBG("%sabling eSPI in response to AP power event",
			enable ? "en" : "dis");
		espi_it8xxx2_enable_pad_ctrl(espi_device, enable);
		break;
	}
	default:
		__ASSERT(false, "%s: unhandled event: %d", __func__,
			 data.event);
		break;
	}
}

static int init_espi_enable_callback(const struct device *unused)
{
	static struct ap_power_ev_callback cb;

	if (!device_is_ready(espi_device))
		k_oops();

	ap_power_ev_init_callback(&cb, espi_enable_callback,
				  AP_POWER_INITIALIZED | AP_POWER_PRE_INIT |
					  AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&cb);

	return 0;
}
/* Should run before power sequencing init so INITIALIZED callback can fire */
SYS_INIT(init_espi_enable_callback, APPLICATION, 0);
