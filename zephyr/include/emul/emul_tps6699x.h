/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_TPS6699X_H_
#define __EMUL_TPS6699X_H_

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

struct tps6699x_emul_pdc_data {
	struct gpio_dt_spec irq_gpios;
	uint32_t delay_ms;
	uint8_t reg_addr;
	/* There are 0xa4 registers, and the biggest is 512 bits long.
	 * TODO(b/345292002): Define a real data structure for registers.
	 */
	uint8_t reg_val[0xa4][64];
};

#endif /* __EMUL_TPS6699X_H_ */
