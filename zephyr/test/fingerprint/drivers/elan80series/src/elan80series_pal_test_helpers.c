/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "elan80series_pal_test_helpers.h"

int z_impl_elan80series_pal_usleep(unsigned int us)
{
	return elan_usleep(us);
}

void *z_impl_elan80series_pal_malloc(uint32_t size)
{
	return elan_malloc(size);
}

void z_impl_elan80series_pal_free(void *data)
{
	return elan_free(data);
}

uint32_t z_impl_elan80series_pal_get_tick(void)
{
	return elan_get_tick();
}

void z_impl_elan80series_pal_sensor_set_rst(bool state)
{
	return elan_sensor_set_rst(state);
}

int z_impl_elan80series_pal_read_register(uint8_t regaddr, uint8_t *regdata)
{
	return elan_read_register(regaddr, regdata);
}

int z_impl_elan80series_pal_read_cmd(uint8_t fp_cmd, uint8_t *regdata)
{
	return elan_read_cmd(fp_cmd, regdata);
}
