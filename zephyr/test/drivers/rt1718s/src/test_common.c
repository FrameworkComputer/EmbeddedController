/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/tcpc/emul_rt1718s.h"
#include "test/drivers/stubs.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define RT1718S_NODE DT_NODELABEL(rt1718s_emul)

const int tcpm_rt1718s_port = USBC_PORT_C0;
const struct emul *rt1718s_emul = EMUL_DT_GET(RT1718S_NODE);

void rt1718s_clear_set_reg_history(void *f)
{
	rt1718s_emul_reset_set_history(rt1718s_emul);
}

static uint16_t get_emul_reg(const struct emul *emul, int reg)
{
	uint16_t val;

	zassert_ok(rt1718s_emul_get_reg(emul, reg, &val),
		   "Cannot get reg %x on rt1718s emul", reg);
	return val;
}

void compare_reg_val_with_mask(const struct emul *emul, int reg,
			       uint16_t expected, uint16_t mask)
{
	uint16_t masked_val = get_emul_reg(emul, reg) & mask;
	uint16_t masked_expected = expected & mask;

	zassert_equal(masked_val, masked_expected,
		      "expected register %x with mask %x should be %x, get %x",
		      reg, mask, masked_expected, masked_val);
}
