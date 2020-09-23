/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Console command to query USART state
 */
#include "atomic.h"
#include "common.h"
#include "console.h"
#include "usart.h"

static int command_usart_info(int argc, char **argv)
{
	struct usart_configs configs = usart_get_configs();
	size_t i;

	for (i = 0; i < configs.count; i++) {
		struct usart_config const *config = configs.configs[i];

		if (config == NULL)
			continue;

		ccprintf("USART%d\n"
			 "    dropped %d bytes\n"
			 "    overran %d times\n",
			 config->hw->index + 1,
			 deprecated_atomic_read_clear(
				 &(config->state->rx_dropped)),
			 deprecated_atomic_read_clear(
				 &(config->state->rx_overrun)));

		if (config->rx->info)
			config->rx->info(config);

		if (config->tx->info)
			config->tx->info(config);
	}

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(usart_info,
			command_usart_info,
			NULL,
			"Display USART info");
