/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is a work-in-progress shim that is currently only validated by the
 * native posix tests. The WIP shim here will be iterated on until it is
 * sufficient to replace the existing battery shim.
 */

#include "battery.h"

#include <zephyr/drivers/fuel_gauge.h>

void battery_get_params(struct batt_params *batt)
{
	/* TODO rename upstream_battery to default_battery */
	const struct device *dev =
		DEVICE_DT_GET(DT_NODELABEL(upstream_battery));

	union fuel_gauge_prop_val raw_voltage;
	union fuel_gauge_prop_val raw_current;

	int ret;

	ret = fuel_gauge_get_prop(dev, FUEL_GAUGE_VOLTAGE, &raw_voltage);
	if (ret) {
		batt->status = ret;
		return;
	}
	ret = fuel_gauge_get_prop(dev, FUEL_GAUGE_CURRENT, &raw_current);
	if (ret) {
		batt->status = ret;
		return;
	}

	batt->voltage = raw_voltage.voltage / 1000;
	batt->current = raw_current.current / 1000;

	/*
	 * TODO(b/271889974): Percolate failed properties to client.
	 */
}

int board_cut_off_battery(void)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_NODELABEL(upstream_battery));

	return fuel_gauge_battery_cutoff(dev);
}
