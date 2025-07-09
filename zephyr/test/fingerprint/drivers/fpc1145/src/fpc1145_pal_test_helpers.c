/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc1145_pal_test_helpers.h"

int z_impl_fpc1145_pal_spi_writeread(fpc_device_t device, uint8_t *tx_buffer,
				     uint8_t *rx_buffer, uint32_t size)
{
	return fpc_pal_spi_writeread(device, tx_buffer, rx_buffer, size);
}

int z_impl_fpc1145_pal_wait_irq(fpc_device_t device, enum fpc_pal_irq irq_type)
{
	return fpc_pal_wait_irq(device, irq_type);
}

int z_impl_fpc1145_pal_get_time(uint64_t *time_us)
{
	return fpc_pal_get_time(time_us);
}

int z_impl_fpc1145_pal_delay_us(uint64_t us)
{
	return fpc_pal_delay_us(us);
}
