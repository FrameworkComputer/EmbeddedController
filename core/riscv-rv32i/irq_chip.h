/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Chip-specific part of the IRQ handling.
 */

#ifndef __CROS_EC_IRQ_CHIP_H
#define __CROS_EC_IRQ_CHIP_H

/**
 * Enable an IRQ in the chip interrupt controller.
 *
 * @param irq interrupt request index.
 */
void chip_enable_irq(int irq);

/**
 * Disable an IRQ in the chip interrupt controller.
 *
 * @param irq interrupt request index.
 */
void chip_disable_irq(int irq);

/**
 * Clear a pending IRQ in the chip interrupt controller.
 *
 * @param irq interrupt request index.
 *
 * Note that most interrupts can be removed from the pending state simply by
 * handling whatever caused the interrupt in the first place.  This only needs
 * to be called if an interrupt handler disables itself without clearing the
 * reason for the interrupt, and then the interrupt is re-enabled from a
 * different context.
 */
void chip_clear_pending_irq(int irq);

/**
 * Software-trigger an IRQ in the chip interrupt controller.
 *
 * @param irq interrupt request index.
 * @return CPU interrupt number to trigger if any, -1 else.
 */
int chip_trigger_irq(int irq);

/**
 * Initialize chip interrupt controller.
 */
void chip_init_irqs(void);

/**
 * Return external interrupt number.
 */
int chip_get_ec_int(void);

/**
 * Return group number of the given external interrupt number.
 */
int chip_get_intc_group(int irq);

#endif /* __CROS_EC_IRQ_CHIP_H */
