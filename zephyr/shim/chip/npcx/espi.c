/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <sys/util.h>

#include "cros_version.h"
#include "drivers/espi.h"
#include "soc_espi.h"
#include "zephyr_espi_shim.h"

bool is_acpi_command(uint32_t data)
{
#if IS_ZEPHYR_VERSION(2, 6)
	struct espi_evt_data_acpi *acpi = (struct espi_evt_data_acpi *)&data;

	return acpi->type;
#elif IS_ZEPHYR_VERSION(2, 5)
	return (data >> NPCX_ACPI_TYPE_POS) & 0x01;
#endif
}

uint32_t get_acpi_value(uint32_t data)
{
#if IS_ZEPHYR_VERSION(2, 6)
	struct espi_evt_data_acpi *acpi = (struct espi_evt_data_acpi *)&data;

	return acpi->data;
#elif IS_ZEPHYR_VERSION(2, 5)
	return (data >> NPCX_ACPI_DATA_POS) & 0xff;
#endif
}

bool is_8042_ibf(uint32_t data)
{
#if IS_ZEPHYR_VERSION(2, 6)
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->evt & HOST_KBC_EVT_IBF;
#elif IS_ZEPHYR_VERSION(2, 5)
	return (data >> NPCX_8042_EVT_POS) & NPCX_8042_EVT_IBF;
#endif
}

bool is_8042_obe(uint32_t data)
{
#if IS_ZEPHYR_VERSION(2, 6)
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->evt & HOST_KBC_EVT_OBE;
#elif IS_ZEPHYR_VERSION(2, 5)
	return (data >> NPCX_8042_EVT_POS) & NPCX_8042_EVT_OBE;
#endif
}

uint32_t get_8042_type(uint32_t data)
{
#if IS_ZEPHYR_VERSION(2, 6)
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->type;
#elif IS_ZEPHYR_VERSION(2, 5)
	return (data >> NPCX_8042_TYPE_POS) & 0xFF;
#endif
}

uint32_t get_8042_data(uint32_t data)
{
#if IS_ZEPHYR_VERSION(2, 6)
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->data;
#elif IS_ZEPHYR_VERSION(2, 5)
	return (data >> NPCX_8042_DATA_POS) & 0xFF;
#endif
}
