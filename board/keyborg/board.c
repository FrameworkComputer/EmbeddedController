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
#include "util.h"

int main(void)
{
	int i = 0;
	hardware_init();
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
		}
	}
}
