/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc1025_pal_test_helpers.h"

#include <fpc1025_pal.h>

int z_impl_fpc1025_pal_spi_write_read(uint8_t *write, uint8_t *read,
				      size_t size, bool leave_cs_asserted)
{
	return fpc_sensor_spi_write_read(write, read, size, leave_cs_asserted);
}

bool z_impl_fpc1025_pal_spi_check_irq(void)
{
	return fpc_sensor_spi_check_irq();
}

bool z_impl_fpc1025_pal_spi_read_irq(void)
{
	return fpc_sensor_spi_read_irq();
}

void z_impl_fpc1025_pal_spi_reset(bool state)
{
	return fpc_sensor_spi_reset(state);
}

uint32_t z_impl_fpc1025_pal_timebase_get_tick(void)
{
	return fpc_timebase_get_tick();
}

void z_impl_fpc1025_pal_timebase_busy_wait(uint32_t ms)
{
	return fpc_timebase_busy_wait(ms);
}

void *z_impl_fpc1025_pal_malloc(uint32_t size)
{
	return fpc_malloc(size);
}

void z_impl_fpc1025_pal_free(void *data)
{
	return fpc_free(data);
}
