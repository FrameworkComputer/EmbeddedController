/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Driver for AMD STB dump functionality */
#include "chipset.h"
#include "driver/amd_stb.h"
#include "hooks.h"

#ifndef CONFIG_ZTEST
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#else
#define CPRINTS(format, args...) printk(format, ##args)
#endif

static bool stb_dump_in_progress;
static struct {
	/* Interrupt from EC to AP. */
	const struct gpio_dt_spec *int_out;
	/* Interrupt from AP to EC. */
	const struct gpio_dt_spec *int_in;
} stb_dump_config;

void amd_stb_dump_finish(void)
{
	gpio_pin_set_dt(stb_dump_config.int_out, 0);
	stb_dump_in_progress = false;
}

void amd_stb_dump_trigger(void)
{
	if (stb_dump_in_progress || !stb_dump_config.int_out)
		return;

	CPRINTS("Triggering STB dump");
	stb_dump_in_progress = true;
	gpio_pin_set_dt(stb_dump_config.int_out, 1);
}

void amd_stb_dump_init(const struct gpio_dt_spec *int_out,
		       const struct gpio_dt_spec *int_in)
{
	stb_dump_config.int_out = int_out;
	stb_dump_config.int_in = int_in;
}

static void stb_dump_interrupt_deferred(void)
{
	/* AP has indicated that it has finished the dump. */
	if (!stb_dump_in_progress)
		return;

	amd_stb_dump_finish();
	CPRINTS("STB dump finished");
}
DECLARE_DEFERRED(stb_dump_interrupt_deferred);

bool amd_stb_dump_in_progress(void)
{
	return stb_dump_in_progress;
}

void amd_stb_dump_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&stb_dump_interrupt_deferred_data, 0);
}

static int command_amdstbdump(int argc, const char **argv)
{
	amd_stb_dump_trigger();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(amdstbdump, command_amdstbdump, NULL,
			"Trigger an STB dump");
