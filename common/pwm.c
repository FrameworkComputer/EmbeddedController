/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "pwm.h"
#include "util.h"

/**
 * Print status of a PWM channel.
 *
 * @param ch		Channel to print.
 */
static void print_channel(enum pwm_channel ch)
{
	if (pwm_get_enabled(ch))
		ccprintf("  %d: %d%%\n", ch, pwm_get_duty(ch));
	else
		ccprintf("  %d: disabled\n", ch);
}

static int cc_pwm_duty(int argc, char **argv)
{
	int percent = 0;
	int ch;
	char *e;

	if (argc < 2) {
		ccprintf("PWM channels:\n");
		for (ch = 0; ch < PWM_CH_COUNT; ch++)
			print_channel(ch);
		return EC_SUCCESS;
	}

	ch = strtoi(argv[1], &e, 0);
	if (*e || ch < 0 || ch >= PWM_CH_COUNT)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		percent = strtoi(argv[2], &e, 0);
		if (*e || percent > 100) {
			/* Bad param */
			return EC_ERROR_PARAM1;
		} else if (percent < 0) {
			/* Negative = disable */
			pwm_enable(ch, 0);
		} else {
			ccprintf("Setting channel %d to %d%%\n", ch, percent);
			pwm_enable(ch, 1);
			pwm_set_duty(ch, percent);
		}
	}

	print_channel(ch);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pwmduty, cc_pwm_duty,
			"[channel [<percent> | -1=disable]]",
			"Get/set PWM duty cycles ",
			NULL);
