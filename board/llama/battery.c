#include "battery.h"

static const struct battery_info info = {
	.voltage_max = 8700,
	.voltage_normal = 7600,
	.voltage_min = 6000,

	/* Pre-charge values. */
	.precharge_current = 256,	/* mA */

	.start_charging_min_c = 0,
	.start_charging_max_c = 45,
	.charging_min_c = 0,
	.charging_max_c = 45,
	.discharging_min_c = -10,
	.discharging_max_c = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}
