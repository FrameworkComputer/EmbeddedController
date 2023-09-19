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

	struct fuel_gauge_property props[] = {
		{
			.property_type = FUEL_GAUGE_VOLTAGE,
		},
		{
			.property_type = FUEL_GAUGE_CURRENT,
		},
	};

	int ret = fuel_gauge_get_prop(dev, props, ARRAY_SIZE(props));

	/* We explicitly expect some properties to fail */
	ARG_UNUSED(ret);

	batt->voltage = props[0].value.voltage / 1000;
	batt->current = props[1].value.current / 1000;

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
