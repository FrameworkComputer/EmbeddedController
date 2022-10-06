/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IO Expander Controller Common Code */

#include "builtin/assert.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "system.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ##args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ##args)

int signal_is_ioex(int signal)
{
	return ((signal >= IOEX_SIGNAL_START) && (signal < IOEX_SIGNAL_END));
}

static const struct ioex_info *ioex_get_signal_info(enum ioex_signal signal)
{
	const struct ioex_info *g;

	ASSERT(signal_is_ioex(signal));

	g = ioex_list + signal - IOEX_SIGNAL_START;

	if (!(ioex_config[g->ioex].flags & IOEX_FLAGS_INITIALIZED)) {
		CPRINTS("ioex %s disabled", g->name);
		return NULL;
	}

	return g;
}

static int ioex_is_valid_interrupt_signal(enum ioex_signal signal)
{
	const struct ioexpander_drv *drv;
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	/* Fail if no interrupt handler */
	if (signal - IOEX_SIGNAL_START >= ioex_ih_count)
		return EC_ERROR_PARAM1;

	drv = ioex_config[g->ioex].drv;
	/*
	 * Not every IOEX chip can support interrupt, check it before enabling
	 * the interrupt function
	 */
	if (drv->enable_interrupt == NULL) {
		CPRINTS("IOEX chip port %d doesn't support INT", g->ioex);
		return EC_ERROR_UNIMPLEMENTED;
	}

	return EC_SUCCESS;
}

int ioex_enable_interrupt(enum ioex_signal signal)
{
	int rv;
	const struct ioex_info *g = ioex_get_signal_info(signal);
	const struct ioexpander_drv *drv;

	rv = ioex_is_valid_interrupt_signal(signal);
	if (rv != EC_SUCCESS)
		return rv;

	drv = ioex_config[g->ioex].drv;
	return drv->enable_interrupt(g->ioex, g->port, g->mask, 1);
}

int ioex_disable_interrupt(enum ioex_signal signal)
{
	int rv;
	const struct ioexpander_drv *drv;
	const struct ioex_info *g = ioex_get_signal_info(signal);

	rv = ioex_is_valid_interrupt_signal(signal);
	if (rv != EC_SUCCESS)
		return rv;

	drv = ioex_config[g->ioex].drv;
	return drv->enable_interrupt(g->ioex, g->port, g->mask, 0);
}

int ioex_get_ioex_flags(enum ioex_signal signal, int *val)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	*val = ioex_config[g->ioex].flags;

	return EC_SUCCESS;
}

int ioex_get_flags(enum ioex_signal signal, int *flags)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->get_flags_by_mask(g->ioex, g->port,
							   g->mask, flags);
}

int ioex_set_flags(enum ioex_signal signal, int flags)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->set_flags_by_mask(g->ioex, g->port,
							   g->mask, flags);
}

int ioex_get_level(enum ioex_signal signal, int *val)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->get_level(g->ioex, g->port, g->mask,
						   val);
}

int ioex_set_level(enum ioex_signal signal, int value)
{
	const struct ioex_info *g = ioex_get_signal_info(signal);

	if (g == NULL)
		return EC_ERROR_BUSY;

	return ioex_config[g->ioex].drv->set_level(g->ioex, g->port, g->mask,
						   value);
}

#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
int ioex_get_port(int ioex, int port, int *val)
{
	if (ioex_config[ioex].drv->get_port == NULL)
		return EC_ERROR_UNIMPLEMENTED;

	return ioex_config[ioex].drv->get_port(ioex, port, val);
}
#endif

int ioex_save_gpio_state(int ioex, int *state, int state_len)
{
	int rv;
	const struct ioex_info *g = ioex_list;
	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int state_offset = 0;

	for (int i = 0; i < IOEX_COUNT; i++, g++) {
		if (g->ioex != ioex)
			continue;

		if (state_offset >= state_len) {
			CPRINTS("%s state buffer is too small", __func__);
			return EC_ERROR_UNKNOWN;
		}

		rv = drv->get_flags_by_mask(g->ioex, g->port, g->mask,
					    &state[state_offset++]);
		if (rv) {
			CPRINTS("%s failed to get flags rv=%d", __func__, rv);
			return rv;
		}
	}

	return EC_SUCCESS;
}

int ioex_restore_gpio_state(int ioex, const int *state, int state_len)
{
	int rv;
	const struct ioex_info *g = ioex_list;
	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int state_offset = 0;

	for (int i = 0; i < IOEX_COUNT; i++, g++) {
		if (g->ioex != ioex)
			continue;

		if (state_offset >= state_len) {
			CPRINTS("%s state buffer is too small", __func__);
			return EC_ERROR_UNKNOWN;
		}

		rv = drv->set_flags_by_mask(g->ioex, g->port, g->mask,
					    state[state_offset++]);
		if (rv) {
			CPRINTS("%s failed to set flags rv=%d", __func__, rv);
			return rv;
		}
	}

	return EC_SUCCESS;
}

int ioex_init(int ioex)
{
	const struct ioex_info *g = ioex_list;
	const struct ioexpander_drv *drv = ioex_config[ioex].drv;
	int rv;
	int i;

	if (drv->init != NULL) {
		rv = drv->init(ioex);
		if (rv != EC_SUCCESS)
			return rv;
	}

	/*
	 * Set all IO expander GPIOs to default flags according to the setting
	 * in gpio.inc
	 */
	for (i = 0; i < IOEX_COUNT; i++, g++) {
		int flags = g->flags;

		if (g->ioex == ioex && g->mask && !(flags & GPIO_DEFAULT)) {
			/* Late-sysJump should not set the output levels */
			if (system_jumped_late())
				flags &= ~(GPIO_LOW | GPIO_HIGH);

			drv->set_flags_by_mask(g->ioex, g->port, g->mask,
					       flags);
		}
	}

	ioex_config[ioex].flags &= ~IOEX_FLAGS_DEFAULT_INIT_DISABLED;
	ioex_config[ioex].flags |= IOEX_FLAGS_INITIALIZED;

	return EC_SUCCESS;
}

static void ioex_init_default(void)
{
	int i;

	for (i = 0; i < CONFIG_IO_EXPANDER_PORT_COUNT; i++) {
		/*
		 * If the IO Expander has been initialized or if the default
		 * initialization is disabled, skip initializing.
		 */
		if (ioex_config[i].flags &
		    (IOEX_FLAGS_INITIALIZED | IOEX_FLAGS_DEFAULT_INIT_DISABLED))
			continue;

		ioex_init(i);
	}
}
DECLARE_HOOK(HOOK_INIT, ioex_init_default, HOOK_PRIO_INIT_I2C + 1);

const char *ioex_get_name(enum ioex_signal signal)
{
	const struct ioex_info *g = ioex_list + signal - IOEX_SIGNAL_START;

	return g->name;
}
