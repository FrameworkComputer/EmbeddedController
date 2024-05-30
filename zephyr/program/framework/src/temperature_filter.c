
#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>



#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>


#include "console.h"
#include "zephyr_console_shim.h"
#include "i2c_hid.h"
#include "hooks.h"
#include "host_command.h"
#include "util.h"
#include "math_util.h"

#include "temperature_filter.h"



void thermal_filter_reset(struct biquad *filter)
{
	memset(&filter->state, 30 << IN_SCALE, sizeof(filter->state));
}

int thermal_filter_update(struct biquad *filter, int value)
{
	int out_scaled =
		filter->coeff[0] * (value << IN_SCALE) +
		filter->coeff[1] * filter->state[0] +
		filter->coeff[2] * filter->state[1] -
		filter->coeff[4] * filter->state[2] -
		filter->coeff[5] * filter->state[3];
	int out = out_scaled >> Q_SCALE;
	/* update delay line */
	filter->state[1] = filter->state[0];
	filter->state[3] = filter->state[2];
	filter->state[0] = value << IN_SCALE;
	filter->state[2] = out;
	return out >> IN_SCALE;
}

int thermal_filter_get(struct biquad *filter)
{
	return filter->state[2] >> IN_SCALE;
}
