/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_KTU1125_H
#define EMUL_KTU1125_H

#include <zephyr/drivers/emul.h>
#include <zephyr/sys/slist.h>

/**
 * @brief Function called on reset
 *
 * @param emul Pointer to ktu1125 emulator
 */
void ktu1125_emul_reset(const struct emul *emul);

/**
 * @brief Set value of given register on the ktu1125 PPC.
 *
 * @param emul Pointer to ktu1125 emulator
 * @param reg Register to change
 * @param val New register value
 */
int ktu1125_emul_set_reg(const struct emul *emul, int reg, int val);

/**
 * @brief Assert/deassert interrupt GPIO to the ktu1125 PPC.
 *
 * @param emul Pointer to ktu1125 emulator
 * @param assert_irq State of the interrupt signal
 */
void ktu1125_emul_assert_irq(const struct emul *emul, bool assert_irq);

#endif /* EMUL_KTU1125_H */
