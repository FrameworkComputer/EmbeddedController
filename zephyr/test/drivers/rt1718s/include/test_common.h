/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _RT1718S_TEST_COMMON_
#define _RT1718S_TEST_COMMON_

#include <zephyr/drivers/emul.h>

extern const int tcpm_rt1718s_port;
extern const struct emul *rt1718s_emul;

void rt1718s_clear_set_reg_history(void *f);

void compare_reg_val_with_mask(const struct emul *emul, int reg,
			       uint16_t expected, uint16_t mask);

#endif /* _RT1718S_TEST_COMMON_ */
