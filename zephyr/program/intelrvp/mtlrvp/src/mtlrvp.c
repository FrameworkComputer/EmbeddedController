/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "intel_rvp_board_id.h"
#include "intelrvp.h"
#include "keyboard_raw.h"
#include "task.h"

#include <zephyr/drivers/espi.h>

#define CPRINTF(format, args...) cprintf(CC_COMMAND, format, ##args)
#define CPRINTS(format, args...) cprints(CC_COMMAND, format, ##args)

/* eSPI device */
#define espi_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_espi))

__override void board_overcurrent_event(int port, int is_overcurrented)
{
	/*
	 * Meteorlake PCH uses Virtual Wire for over current error,
	 * hence Send 'Over Current Virtual Wire' eSPI signal.
	 */
	espi_send_vwire(espi_dev, port + ESPI_VWIRE_SIGNAL_SLV_GPIO_0,
			!is_overcurrented);
}

/******************************************************************************/
/* KSO mapping for discrete keyboard */
__override const uint8_t it8801_kso_mapping[] = {
	0, 1, 20, 3, 4, 5, 6, 11, 12, 13, 14, 15, 16,
};
BUILD_ASSERT(ARRAY_SIZE(it8801_kso_mapping) == KEYBOARD_COLS_MAX);

/*
 * Returns board information (board id[7:0] and Fab id[15:8]) on success
 * -1 on error.
 */
__override int board_get_version(void)
{
	/* Cache the MTLRVP board ID */
	static int mtlrvp_board_id;

	int i;
	int rv = EC_ERROR_UNKNOWN;
	int fab_id, board_id, bom_id;

	/* Board ID is already read */
	if (mtlrvp_board_id)
		return mtlrvp_board_id;

	/*
	 * IOExpander that has Board ID information is on DSW-VAL rail on
	 * MTL RVP. On cold boot cycles, DSW-VAL rail is taking time to settle.
	 * This loop retries to ensure rail is settled and read is successful
	 */
	for (i = 0; i < RVP_VERSION_READ_RETRY_CNT; i++) {
		rv = gpio_pin_get_dt(&bom_id_config[0]);

		if (rv >= 0)
			break;

		k_msleep(1);
	}

	/* return -1 if failed to read board id */
	if (rv)
		return -1;

	/*
	 * BOM ID [2]   : IOEX[0]
	 * BOM ID [1:0] : IOEX[15:14]
	 */
	bom_id = gpio_pin_get_dt(&bom_id_config[0]) << 2;
	bom_id |= gpio_pin_get_dt(&bom_id_config[1]) << 1;
	bom_id |= gpio_pin_get_dt(&bom_id_config[2]);
	/*
	 * FAB ID [1:0] : IOEX[2:1] + 1
	 */
	fab_id = gpio_pin_get_dt(&fab_id_config[0]) << 1;
	fab_id |= gpio_pin_get_dt(&fab_id_config[1]);
	fab_id += 1;

	/*
	 * BOARD ID[5:0] : IOEX[13:8]
	 */
	board_id = gpio_pin_get_dt(&board_id_config[0]) << 5;
	board_id |= gpio_pin_get_dt(&board_id_config[1]) << 4;
	board_id |= gpio_pin_get_dt(&board_id_config[2]) << 3;
	board_id |= gpio_pin_get_dt(&board_id_config[3]) << 2;
	board_id |= gpio_pin_get_dt(&board_id_config[4]) << 1;
	board_id |= gpio_pin_get_dt(&board_id_config[5]);

	CPRINTF("BID:0x%x, FID:0x%x, BOM:0x%x", board_id, fab_id, bom_id);

	mtlrvp_board_id = board_id | (fab_id << 8);
	return mtlrvp_board_id;
}
