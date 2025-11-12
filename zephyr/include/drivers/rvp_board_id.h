/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file drivers/rvp_board_id.h
 * @brief Public APIs for board id driver for Intel RVPs.
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_RVP_BOARD_ID_H_
#define ZEPHYR_INCLUDE_DRIVERS_RVP_BOARD_ID_H_

/**
 * @brief RVP board identification types
 */
enum rvp_id_type {
	BOARD_ID = 0,
	BOM_ID,
	FAB_ID,
};

/**
 * @brief RVP board identification callback prototype.
 *
 * This is the function type associated with the "handler" property in
 * the "intel,rvp-board-id" device tree node.
 */
typedef void (*rvp_board_id_handler)(void);

/**
 * @brief Get RVP board identification configuration
 *
 * Reads GPIO pins to determine board ID, BOM ID, or FAB ID based on the
 * requested type. The function reads specific GPIO configurations and
 * combines them into the appropriate identification value.
 *
 * @param id_type Type of ID to retrieve (BOARD_ID, BOM_ID, or FAB_ID)
 *
 * @return On success, returns the requested ID value (board_id, bom_id, or
 * fab_id)
 * @return -1 on error (invalid id_type or GPIO controller not ready)
 */
int get_rvp_id_config(enum rvp_id_type id_type);

#endif /* ZEPHYR_INCLUDE_DRIVERS_RVP_BOARD_ID_H_ */
