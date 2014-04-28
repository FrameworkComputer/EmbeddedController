/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Keyborg board-specific configuration */

#include "board.h"
#include "common.h"
#include "debug.h"
#include "master_slave.h"
#include "registers.h"
#include "spi_comm.h"
#include "system.h"
#include "task.h"
#include "touch_scan.h"
#include "util.h"

const struct ts_pin row_pins[] = {
	{TS_GPIO_E,  0}, /* R1 */
	{TS_GPIO_E,  4}, /* R2 */
	{TS_GPIO_E,  8}, /* R3 */
	{TS_GPIO_E,  1}, /* R4 */
	{TS_GPIO_E, 11}, /* R5 */
	{TS_GPIO_E, 12}, /* R6 */
	{TS_GPIO_E, 15}, /* R7 */
	{TS_GPIO_E, 13}, /* R8 */
	{TS_GPIO_D,  3}, /* R9 */
	{TS_GPIO_D,  4}, /* R10 */
	{TS_GPIO_D,  5}, /* R11 */
	{TS_GPIO_D,  0}, /* R12 */
	{TS_GPIO_D,  6}, /* R13 */
	{TS_GPIO_D,  8}, /* R14 */
	{TS_GPIO_D, 11}, /* R15 */
	{TS_GPIO_D, 10}, /* R16 */
	{TS_GPIO_D, 12}, /* R17 */
	{TS_GPIO_D, 13}, /* R18 */
	{TS_GPIO_D, 14}, /* R19 */
	{TS_GPIO_D, 15}, /* R20 */
	{TS_GPIO_C,  8}, /* R21 */
	{TS_GPIO_C,  7}, /* R22 */
	{TS_GPIO_C, 15}, /* R23 */
	{TS_GPIO_E,  6}, /* R24 */
	{TS_GPIO_E,  5}, /* R25 */
	{TS_GPIO_E,  2}, /* R26 */
	{TS_GPIO_E,  3}, /* R27 */
	{TS_GPIO_E, 10}, /* R28 */
	{TS_GPIO_E,  9}, /* R29 */
	{TS_GPIO_E, 14}, /* R30 */
	{TS_GPIO_E,  7}, /* R31 */
	{TS_GPIO_D,  2}, /* R32 */
	{TS_GPIO_D,  7}, /* R33 */
	{TS_GPIO_D,  1}, /* R34 */
	{TS_GPIO_D,  9}, /* R35 */
	{TS_GPIO_C,  5}, /* R36 */
	{TS_GPIO_C,  6}, /* R37 */
	{TS_GPIO_C, 10}, /* R38 */
	{TS_GPIO_C, 13}, /* R39 */
	{TS_GPIO_C, 14}, /* R40 */
	{TS_GPIO_C, 12}, /* R41 */
};
BUILD_ASSERT(ARRAY_SIZE(row_pins) == ROW_COUNT);

const struct ts_pin col_pins[] = {
	{TS_GPIO_B,  5}, /* C1 */
	{TS_GPIO_H,  1}, /* C2 */
	{TS_GPIO_H,  0}, /* C3 */
	{TS_GPIO_H,  5}, /* C4 */
	{TS_GPIO_H, 10}, /* C5 */
	{TS_GPIO_H,  6}, /* C6 */
	{TS_GPIO_H,  4}, /* C7 */
	{TS_GPIO_H,  3}, /* C8 */
	{TS_GPIO_H,  9}, /* C9 */
	{TS_GPIO_H, 12}, /* C10 */
	{TS_GPIO_H, 11}, /* C11 */
	{TS_GPIO_H, 15}, /* C12 */
	{TS_GPIO_H,  2}, /* C13 */
	{TS_GPIO_H, 14}, /* C14 */
	{TS_GPIO_G,  5}, /* C15 */
	{TS_GPIO_G,  9}, /* C16 */
	{TS_GPIO_G,  4}, /* C17 */
	{TS_GPIO_G, 15}, /* C18 */
	{TS_GPIO_G, 10}, /* C19 */
	{TS_GPIO_G, 12}, /* C20 */
	{TS_GPIO_G,  0}, /* C21 */
	{TS_GPIO_G, 11}, /* C22 */
	{TS_GPIO_B,  0}, /* C23 */
	{TS_GPIO_G,  2}, /* C24 */
	{TS_GPIO_G,  1}, /* C25 */
	{TS_GPIO_A, 13}, /* C26 */
	{TS_GPIO_A, 14}, /* C27 */
	{TS_GPIO_B,  3}, /* C28 */
	{TS_GPIO_A, 10}, /* Fake C29. C29 is used as UART Tx. */
	{TS_GPIO_B,  8}, /* C30 */
	{TS_GPIO_A, 10}, /* C31 */
	{TS_GPIO_B,  1}, /* C32 */
	{TS_GPIO_G, 13}, /* C33 */
	{TS_GPIO_B,  7}, /* C34 */
	{TS_GPIO_B,  2}, /* C35 */
	{TS_GPIO_G, 14}, /* C36 */
	{TS_GPIO_G,  3}, /* C37 */
	{TS_GPIO_G,  7}, /* C38 */
	{TS_GPIO_H, 13}, /* C39 */
	{TS_GPIO_H,  7}, /* C40 */
	{TS_GPIO_B,  4}, /* C41 */
	{TS_GPIO_H,  8}, /* C42 */
	{TS_GPIO_B,  6}, /* C43 */
	{TS_GPIO_B,  9}, /* C44 */
	{TS_GPIO_I, 10}, /* C45 */
	{TS_GPIO_I, 11}, /* C46 */
	{TS_GPIO_I,  9}, /* C47 */
	{TS_GPIO_G,  8}, /* C48 */
	{TS_GPIO_G,  6}, /* C49 */
	{TS_GPIO_I,  4}, /* C50 */
	{TS_GPIO_I,  3}, /* C51 */
	{TS_GPIO_I,  5}, /* C52 */
	{TS_GPIO_I, 14}, /* C53 */
	{TS_GPIO_I, 12}, /* C54 */
	{TS_GPIO_I,  8}, /* C55 */
	{TS_GPIO_I,  6}, /* C56 */
	{TS_GPIO_I, 15}, /* C57 */
	{TS_GPIO_I,  0}, /* C58 */
	{TS_GPIO_I, 13}, /* C59 */
	{TS_GPIO_I,  7}, /* C60 */
};
BUILD_ASSERT(ARRAY_SIZE(col_pins) == COL_COUNT);

int main(void)
{
	int i = 0;
	hardware_init();
	touch_scan_init();
	debug_printf("Keyborg starting...\n");

	master_slave_init();

	/* We want master SPI_NSS low and slave SPI_NSS high */
	STM32_GPIO_BSRR(GPIO_A) = (1 << (1 + 16)) | (1 << 6);

	master_slave_sync(10);

	if (master_slave_is_master())
		spi_master_init();
	else
		spi_slave_init();

	master_slave_sync(100);

	while (1) {
		i++;
		task_wait_event(SECOND);
		if (master_slave_is_master()) {
			debug_printf("Hello x 50...");
			if (spi_hello_test(50) == EC_SUCCESS)
				debug_printf("Passed\n");
			else
				debug_printf("Failed\n");
			task_wait_event(SECOND);
			debug_printf("Scan...");
			if (touch_scan_full_matrix() == EC_SUCCESS)
				debug_printf("Passed\n");
			else {
				debug_printf("Failed\n");
				task_wait_event(-1);
			}
		}
	}
}
