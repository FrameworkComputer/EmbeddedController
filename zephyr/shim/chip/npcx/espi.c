/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <sys/util.h>

#include "drivers/espi.h"
#include "soc_espi.h"
#include "zephyr_espi_shim.h"

bool is_acpi_command(uint32_t data)
{
	struct espi_evt_data_acpi *acpi = (struct espi_evt_data_acpi *)&data;

	return acpi->type;
}

uint32_t get_acpi_value(uint32_t data)
{
	struct espi_evt_data_acpi *acpi = (struct espi_evt_data_acpi *)&data;

	return acpi->data;
}

bool is_8042_ibf(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->evt & HOST_KBC_EVT_IBF;
}

bool is_8042_obe(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->evt & HOST_KBC_EVT_OBE;
}

uint32_t get_8042_type(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->type;
}

uint32_t get_8042_data(uint32_t data)
{
	struct espi_evt_data_kbc *kbc = (struct espi_evt_data_kbc *)&data;

	return kbc->data;
}
