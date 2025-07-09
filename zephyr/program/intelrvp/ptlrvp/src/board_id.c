/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "intel_rvp_board_id.h"
#include "intelrvp.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <power_signals.h>
#ifdef CONFIG_AP_PWRSEQ_DRIVER
#include <ap_power/ap_pwrseq_sm.h>
#endif

LOG_MODULE_REGISTER(board_id, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_NODELABEL(pca95xx_0))
static void pca95xx_deferred_init_cb(const struct device *dev,
				     const enum ap_pwrseq_state entry,
				     const enum ap_pwrseq_state exit)
{
	const struct device *pca95xx;

	if (exit == AP_POWER_STATE_G3) {
		LOG_INF("S5 callback triggered, when exiting G3");
		pca95xx = DEVICE_DT_GET(DT_NODELABEL(pca95xx_0));
		if (!device_is_ready(pca95xx)) {
			LOG_INF("Initializing PCA95XX Ioexpander");
			device_init(pca95xx);
		}
	}
}

static int setup_pca95xx_init_callback(void)
{
	static struct ap_pwrseq_state_callback ap_pwrseq_entry_cb;
	const struct device *ap_pwrseq_dev = ap_pwrseq_get_instance();

	ap_pwrseq_entry_cb.cb = pca95xx_deferred_init_cb;
	ap_pwrseq_entry_cb.states_bit_mask = BIT(AP_POWER_STATE_S5);

	ap_pwrseq_register_state_entry_callback(ap_pwrseq_dev,
						&ap_pwrseq_entry_cb);

	return 0;
}
SYS_INIT(setup_pca95xx_init_callback, POST_KERNEL,
	 CONFIG_APPLICATION_INIT_PRIORITY);
#endif

/*
 * Returns board information (board id[7:0] and Fab id[15:8]) on success
 * -1 on error.
 */
__override int board_get_version(void)
{
	/* Cache the board ID */
	static int ptl_board_id;

	int i;
	int rv = EC_ERROR_UNKNOWN;
	int board_id;
#if RVP_ID_HAS_BOM_GPIOS
	int bom_id;
#endif
#if RVP_ID_HAS_FAB_GPIOS
	int fab_id;
#endif

	/* Board ID is already read */
	if (ptl_board_id)
		return ptl_board_id;

#if DT_NODE_EXISTS(DT_NODELABEL(pca95xx_0))
	if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(pca95xx_0)))) {
		LOG_ERR("PCA95XX ioexpander not initialized cannot access it");
		return -1;
	}
#endif
	/*
	 * IOExpander that has Board ID information is on PRIM_VR rail on
	 * PTL RVP. On cold boot cycles, PRIM_VR rail is taking time to settle.
	 * This loop retries to ensure rail is settled and read is successful
	 */
	for (i = 0; i < RVP_VERSION_READ_RETRY_CNT; i++) {
		rv = gpio_pin_get_dt(&board_id_config[0]);

		if (rv >= 0)
			break;

		k_msleep(1);
	}

	/* return -1 if failed to read board id */
	if (rv < 0)
		return -1;

	/*
	 * BOARD ID[5:0] : IOEX[13:8]
	 */
	board_id = gpio_pin_get_dt(&board_id_config[0]);
	board_id |= gpio_pin_get_dt(&board_id_config[1]) << 1;
	board_id |= gpio_pin_get_dt(&board_id_config[2]) << 2;
	board_id |= gpio_pin_get_dt(&board_id_config[3]) << 3;
	board_id |= gpio_pin_get_dt(&board_id_config[4]) << 4;
	board_id |= gpio_pin_get_dt(&board_id_config[5]) << 5;

	ptl_board_id = board_id;

	LOG_INF("BOARD_ID:0x%x", board_id);
#if RVP_ID_HAS_BOM_GPIOS
	/*
	 * BOM ID [2]   : IOEX[0]
	 * BOM ID [1:0] : IOEX[15:14]
	 */
	bom_id = gpio_pin_get_dt(&bom_id_config[0]);
	bom_id |= gpio_pin_get_dt(&bom_id_config[1]) << 1;
	bom_id |= gpio_pin_get_dt(&bom_id_config[2]) << 2;

	LOG_INF("BOM_ID:0x%x", bom_id);
#endif
#if RVP_ID_HAS_FAB_GPIOS
	/*
	 * FAB ID [1:0] : IOEX[2:1] + 1
	 */
	fab_id = gpio_pin_get_dt(&fab_id_config[0]);
	fab_id |= gpio_pin_get_dt(&fab_id_config[1]) << 1;
	fab_id += 1;

	LOG_INF("FAB_ID:0x%x", fab_id);

	ptl_board_id |= (fab_id << 8);
#endif

	return ptl_board_id;
}
