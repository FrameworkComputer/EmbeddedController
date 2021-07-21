/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <sys/util.h>

#include "soc_espi.h"
#include "zephyr_espi_shim.h"

bool is_acpi_command(uint32_t data)
{
	return (data >> NPCX_ACPI_TYPE_POS) & 0x01;
}

uint32_t get_acpi_value(uint32_t data)
{
	return (data >> NPCX_ACPI_DATA_POS) & 0xff;
}

bool is_8042_ibf(uint32_t data)
{
	return (data >> NPCX_8042_EVT_POS) & NPCX_8042_EVT_IBF;
}

bool is_8042_obe(uint32_t data)
{
	return (data >> NPCX_8042_EVT_POS) & NPCX_8042_EVT_OBE;
}

uint32_t get_8042_type(uint32_t data)
{
	return (data >> NPCX_8042_TYPE_POS) & 0xFF;
}

uint32_t get_8042_data(uint32_t data)
{
	return (data >> NPCX_8042_DATA_POS) & 0xFF;
}
