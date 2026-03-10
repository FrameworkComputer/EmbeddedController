/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "egis660_pal_test_helpers.h"

#include <fingerprint_egis660_pal.h>

int z_impl_egis660_pal_spi_write_read(uint8_t *data, size_t write_size,
				      size_t read_size, bool leave_cs_asserted)
{
	return egis_sensor_spi_write_read(data, write_size, read_size,
					  leave_cs_asserted);
}

bool z_impl_egis660_pal_check_irq(void)
{
	return egis_sensor_check_irq();
}

bool z_impl_egis660_pal_read_irq(void)
{
	return egis_sensor_read_irq();
}

void z_impl_egis660_pal_reset(bool state)
{
	return egis_sensor_reset(state);
}

uint32_t z_impl_egis660_pal_timebase_get_tick(void)
{
	return egis_timebase_get_tick();
}

void z_impl_egis660_pal_timebase_delay_ms(uint32_t ms)
{
	return egis_timebase_delay_ms(ms);
}

void *z_impl_egis660_pal_malloc(uint32_t size)
{
	return egis_malloc(size);
}

void z_impl_egis660_pal_free(void *data)
{
	return egis_free(data);
}
