/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for the PTL-RVP only
 */

#include "drivers/pdc.h"
#include "usbc/pdc_runtime_port_config.h"

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rvp_usbc, LOG_LEVEL_INF);

#define DT_HAS_RTK_PDC DT_HAS_COMPAT_STATUS_OKAY(realtek_rts54_pdc)
#define DT_HAS_TI_PDC DT_HAS_COMPAT_STATUS_OKAY(ti_tps6699_pdc)

#if DT_HAS_RTK_PDC
/* Ports supported by dual PDC chip in AIC */
#define DEV_PDC_C0_RTK DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c0))
#define DEV_PDC_C1_RTK DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c1))

#if CONFIG_USB_PD_PORT_MAX_COUNT > 2
/* Port in modular TCSS AIC */
#define DEV_PDC_C2_RTK DEVICE_DT_GET(DT_NODELABEL(pdc_rtk_c2))
#else
/* named-usbc-port is not declared. */
#define DEV_PDC_C2_RTK NULL
#endif /* CONFIG_USB_PD_PORT_MAX_COUNT > 2 */

#define RVP_TCSS_DUAL_RTK (RVP_TCSS_C0_RTK | RVP_TCSS_C1_RTK)
#endif /* DT_HAS_RTK_PDC */

#if DT_HAS_TI_PDC
/* Ports supported by dual PDC chip in AIC */
#define DEV_PDC_C0_TI DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c0))
#define DEV_PDC_C1_TI DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c1))

#if CONFIG_USB_PD_PORT_MAX_COUNT > 2
/* Port in modular TCSS AIC */
#define DEV_PDC_C2_TI DEVICE_DT_GET(DT_NODELABEL(pdc_ti_c2))
#else
/* named-usbc-port is not declared. */
#define DEV_PDC_C2_TI NULL
#endif

#define RVP_TCSS_DUAL_TI (RVP_TCSS_C0_TI | RVP_TCSS_C1_TI)
#endif /* DT_HAS_TI_PDC */

/**
 * Bitfield for storing detected TCSS modules
 *  - C0 refers to TCP0 that is part of dual port AIC
 *  - C1 refers to TCP1 that is part of dual port AIC
 *  - C2 refers to TCP2 that is in modular AIC
 */
enum rvp_tcss_modules {
	RVP_TCSS_NONE = 0,
	RVP_TCSS_C0_RTK = BIT(0),
	RVP_TCSS_C1_RTK = BIT(1),
	RVP_TCSS_C2_RTK = BIT(2),
	RVP_TCSS_C0_TI = BIT(3),
	RVP_TCSS_C1_TI = BIT(4),
	RVP_TCSS_C2_TI = BIT(5),
};

static struct {
	bool initialized;
	enum rvp_tcss_modules detected_cards;
} ctx;

static bool probe_pdc_chip(const struct device *dev)
{
	struct pdc_hw_config_t config;
	int rv;

	if (dev == NULL) {
		LOG_ERR("%s: Invalid pointer", __func__);
		return false;
	}

	rv = pdc_get_hw_config(dev, &config);
	if (rv) {
		LOG_ERR("%s: Cannot get bus info for PDC %s: %d", __func__,
			dev->name ? dev->name : "unnamed", rv);
		return false;
	}

	struct i2c_msg msgs[1];
	uint8_t dst;

	msgs[0].buf = &dst;
	msgs[0].len = 0U;
	msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	/* If the transfer succeeds, a chip is at this address */
	return i2c_transfer_dt(&config.i2c, &msgs[0], 1) == 0;
}

static void discover_tcss_modules()
{
	if (ctx.initialized) {
		/* Only run once */
		return;
	}

	/* Perform trial I2C operations against each PDC target to see
	 * which are present.
	 */
#if DT_HAS_RTK_PDC
	if (probe_pdc_chip(DEV_PDC_C0_RTK)) {
		ctx.detected_cards |= RVP_TCSS_C0_RTK;
	}
	if (probe_pdc_chip(DEV_PDC_C1_RTK)) {
		ctx.detected_cards |= RVP_TCSS_C1_RTK;
	}
	if (probe_pdc_chip(DEV_PDC_C2_RTK)) {
		ctx.detected_cards |= RVP_TCSS_C2_RTK;
	}
#endif
#if DT_HAS_TI_PDC
	if (probe_pdc_chip(DEV_PDC_C0_TI)) {
		ctx.detected_cards |= RVP_TCSS_C0_TI;
	}
	if (probe_pdc_chip(DEV_PDC_C1_TI)) {
		ctx.detected_cards |= RVP_TCSS_C1_TI;
	}
	if (probe_pdc_chip(DEV_PDC_C2_TI)) {
		ctx.detected_cards |= RVP_TCSS_C2_TI;
	}
#endif

	LOG_INF("%s: TCSS detection result: 0x%02x", __func__,
		ctx.detected_cards);

	ctx.initialized = true;
}

/** Supply pdc_power_mgmt with dynamic USB-C port configuration data */
int board_get_pdc_for_port(int port, const struct device **dev)
{
	discover_tcss_modules();

	/* Based on results, determine the PDC config. Not all combinations
	 * are allowed.
	 */
	if ((int)ctx.detected_cards == 0) {
		/* No TCSS cards. This is supported. */
		LOG_INF("%s: PDC config: [---, ---, ---]", __func__);
	}
#if DT_HAS_RTK_PDC
	else if (((int)ctx.detected_cards & RVP_TCSS_DUAL_RTK) ==
		 RVP_TCSS_DUAL_RTK) {
		if ((int)ctx.detected_cards & RVP_TCSS_C2_RTK) {
			/* Port in single RTK card */
			LOG_INF("%s: PDC config: [RTK, RTK, RTK]", __func__);
			if (port == 2) {
				*dev = DEV_PDC_C2_RTK;
				return 0;
			}
		} else {
			LOG_INF("%s: PDC config: [RTK, RTK, ---]", __func__);
		}
		/* Ports in dual RTK card */
		if (port == 0) {
			*dev = DEV_PDC_C0_RTK;
			return 0;
		} else if (port == 1) {
			*dev = DEV_PDC_C1_RTK;
			return 0;
		}
	}
#endif
#if DT_HAS_TI_PDC
	else if (((int)ctx.detected_cards & RVP_TCSS_DUAL_TI) ==
		 RVP_TCSS_DUAL_TI) {
		if ((int)ctx.detected_cards & RVP_TCSS_C2_TI) {
			/* Port in single TI card */
			LOG_INF("%s: PDC config: [TI , TI, TI ]", __func__);
			if (port == 2) {
				*dev = DEV_PDC_C2_TI;
				return 0;
			}
		} else {
			LOG_INF("%s: PDC config: [TI , TI, --- ]", __func__);
		}
		/* Ports in dual TI card */
		if (port == 0) {
			*dev = DEV_PDC_C0_TI;
			return 0;
		} else if (port == 1) {
			*dev = DEV_PDC_C1_TI;
			return 0;
		}
	}
#endif

	*dev = NULL;
	return -ENOENT;
}
