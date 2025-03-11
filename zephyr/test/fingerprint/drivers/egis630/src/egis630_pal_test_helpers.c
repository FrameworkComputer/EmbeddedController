/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "egis630_pal_test_helpers.h"

uint64_t z_impl_egis630_plat_get_time(void)
{
	return plat_get_time();
}

void z_impl_egis630_plat_wait_time(uint32_t msec)
{
	return plat_wait_time(msec);
}

void z_impl_egis630_plat_sleep_time(uint32_t timeInMs)
{
	return plat_sleep_time(timeInMs);
}

uint32_t z_impl_egis630_plat_get_diff_time(uint64_t begin)
{
	return plat_get_diff_time(begin);
}

void *z_impl_egis630_sys_alloc(size_t count, size_t size)
{
	return sys_alloc(count, size);
}

int z_impl_egis630_periphery_spi_write_read(uint8_t *write, uint32_t size,
					    uint8_t *read, uint32_t rx_len)
{
	return periphery_spi_write_read(write, size, read, rx_len);
}

void z_impl_egis630_output_log(LOG_LEVEL level, const char *tag,
			       const char *file_path, const char *func,
			       int line, const char *format)
{
	return output_log(level, tag, file_path, func, line, format);
}

void z_impl_egis630_set_debug_level(LOG_LEVEL level)
{
	return set_debug_level(level);
}

int z_impl_egis630_rbs_check_if_null(void *ptr, int error_code)
{
	RBS_CHECK_IF_NULL(ptr, error_code);
	return 0; // Return 0 if ptr is not NULL
}
