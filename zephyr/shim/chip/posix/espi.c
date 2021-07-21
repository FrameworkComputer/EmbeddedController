/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/util.h>
#include "zephyr_espi_shim.h"

#define ACPI_TYPE_POS 0U
#define ACPI_DATA_POS 8U

/* 8042 event data format */
#define POSIX_8042_EVT_POS 16U
#define POSIX_8042_DATA_POS 8U
#define POSIX_8042_TYPE_POS 0U

/* 8042 event type format */
#define POSIX_8042_EVT_IBF BIT(0)
#define POSIX_8042_EVT_OBE BIT(1)

bool is_acpi_command(uint32_t data)
{
	return (data >> ACPI_TYPE_POS) & 0x01;
}

uint32_t get_acpi_value(uint32_t data)
{
	return (data >> ACPI_TYPE_POS) & 0xff;
}

bool is_POSIX_8042_ibf(uint32_t data)
{
	return (data >> POSIX_8042_EVT_POS) & POSIX_8042_EVT_IBF;
}

bool is_POSIX_8042_obe(uint32_t data)
{
	return (data >> POSIX_8042_EVT_POS) & POSIX_8042_EVT_OBE;
}

uint32_t get_POSIX_8042_type(uint32_t data)
{
	return (data >> POSIX_8042_TYPE_POS) & 0xFF;
}

uint32_t get_POSIX_8042_data(uint32_t data)
{
	return (data >> POSIX_8042_DATA_POS) & 0xFF;
}
