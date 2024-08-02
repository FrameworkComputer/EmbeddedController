/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_TPS6699X_H_
#define __EMUL_TPS6699X_H_

#include "drivers/ucsi_v3.h"

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

#define TPS6699X_MAX_REG 0xa4
#define TPS6699X_REG_SIZE 64

struct tps6699x_emul_pdc_data {
	struct gpio_dt_spec irq_gpios;
	uint32_t delay_ms;
	/* The register address currently being read or written. */
	uint8_t reg_addr;
	/* The stated length of the current read or write. */
	uint8_t transaction_bytes;
	/* There are 0xa4 registers, and the biggest is 512 bits long.
	 * TODO(b/345292002): Define a real data structure for registers.
	 */
	uint8_t reg_val[TPS6699X_MAX_REG][TPS6699X_REG_SIZE];

	union connector_status_t connector_status;
	union connector_reset_t reset_cmd;
};

#endif /* __EMUL_TPS6699X_H_ */
