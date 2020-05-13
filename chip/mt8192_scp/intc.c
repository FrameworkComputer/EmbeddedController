/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* INTC control module */

int chip_get_ec_int(void)
{
	return 0;
}

int chip_get_intc_group(int irq)
{
	return 0;
}

int chip_enable_irq(int irq)
{
	return 0;
}

int chip_disable_irq(int irq)
{
	return 0;
}

int chip_clear_pending_irq(int irq)
{
	return 0;
}

int chip_trigger_irq(int irq)
{
	return 0;
}

void chip_init_irqs(void)
{
}
