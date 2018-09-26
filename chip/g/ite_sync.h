/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CHIP_G_ITE_SYNC_H
#define __CHIP_G_ITE_SYNC_H

#include "util.h"

/*
 * Assembler function to generates ITE EC sync sequence, which requires two
 * lines generating phase locked 200 KHz and 100 KHz clocks. This is achieved
 * by directly togging two GPIOs.
 *
 * gpio_addr: address of the register to write to drive the GPIOs
 * both_zero:
 * one_zero:
 * zero_one:
 * both_one: values to write at gpio_addr to set the tow lines to these
 *          stattes
 * half_period_ticks: number of interations of the tight loop to last for half
 *          the period of the higher frequency
 * total_ticks_required: total ticks required to generate the sequence of the
 *          necessary duration.
 */
void ite_sync(volatile uint16_t *gpio_addr, uint16_t both_zero,
	      uint16_t one_zero, uint16_t zero_one, uint16_t both_one,
	      uint32_t half_period_ticks, uint32_t total_ticks_required);


/* Generate ITE SYNC sequence on the I2C interface controlling the EC. */
void generate_ite_sync(void);

#endif
