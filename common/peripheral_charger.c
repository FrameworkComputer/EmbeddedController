/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "device_event.h"
#include "hooks.h"
#include "host_command.h"
#include "peripheral_charger.h"
#include "queue.h"
#include "stdbool.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Peripheral Charge Manager */

#define CPRINTS(fmt, args...) cprints(CC_PCHG, "PCHG: " fmt, ##args)

static int dropped_event;

static void pchg_queue_event(struct pchg *ctx, enum pchg_event event)
{
	mutex_lock(&ctx->mtx);
	if (queue_add_unit(&ctx->events, &event) == 0) {
		dropped_event++;
		CPRINTS("ERR: Queue is full");
	}
	mutex_unlock(&ctx->mtx);
}

static const char *_text_state(enum pchg_state state)
{
	/* TODO: Use "S%d" for normal build. */
	static const char * const state_names[] = EC_PCHG_STATE_TEXT;

	if (state >= sizeof(state_names))
		return "UNDEF";

	return state_names[state];
}

static const char *_text_event(enum pchg_event event)
{
	/* TODO: Use "S%d" for normal build. */
	static const char * const event_names[] = {
		[PCHG_EVENT_NONE] = "NONE",
		[PCHG_EVENT_IRQ] = "IRQ",
		[PCHG_EVENT_INITIALIZED] = "INITIALIZED",
		[PCHG_EVENT_ENABLED] = "ENABLED",
		[PCHG_EVENT_DISABLED] = "DISABLED",
		[PCHG_EVENT_DEVICE_DETECTED] = "DEVICE_DETECTED",
		[PCHG_EVENT_DEVICE_LOST] = "DEVICE_LOST",
		[PCHG_EVENT_CHARGE_STARTED] = "CHARGE_STARTED",
		[PCHG_EVENT_CHARGE_UPDATE] = "CHARGE_UPDATE",
		[PCHG_EVENT_CHARGE_ENDED] = "CHARGE_ENDED",
		[PCHG_EVENT_CHARGE_STOPPED] = "CHARGE_STOPPED",
		[PCHG_EVENT_CHARGE_ERROR] = "CHARGE_ERROR",
		[PCHG_EVENT_INITIALIZE] = "INITIALIZE",
		[PCHG_EVENT_ENABLE] = "ENABLE",
		[PCHG_EVENT_DISABLE] = "DISABLE",
	};

	if (event >= sizeof(event_names))
		return "UNDEF";

	return event_names[event];
}

static enum pchg_state pchg_state_reset(struct pchg *ctx)
{
	enum pchg_state state = PCHG_STATE_RESET;
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_INITIALIZE:
		rv = ctx->cfg->drv->init(ctx);
		if (rv == EC_SUCCESS) {
			pchg_queue_event(ctx, PCHG_EVENT_ENABLE);
			state = PCHG_STATE_INITIALIZED;
		} else if (rv != EC_SUCCESS_IN_PROGRESS) {
			CPRINTS("ERR: Failed to initialize");
		}
		break;
	case PCHG_EVENT_INITIALIZED:
		pchg_queue_event(ctx, PCHG_EVENT_ENABLE);
		state = PCHG_STATE_INITIALIZED;
		break;
	default:
		break;
	}

	return state;
}

static enum pchg_state pchg_state_initialized(struct pchg *ctx)
{
	enum pchg_state state = PCHG_STATE_INITIALIZED;
	int rv;

	if (ctx->event == PCHG_EVENT_ENABLE)
		ctx->error &= ~PCHG_ERROR_HOST;

	/* Spin in INITIALIZED until error condition is cleared. */
	if (ctx->error)
		return state;

	switch (ctx->event) {
	case PCHG_EVENT_ENABLE:
		rv = ctx->cfg->drv->enable(ctx, true);
		if (rv == EC_SUCCESS)
			state = PCHG_STATE_ENABLED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to enable");
		break;
	case PCHG_EVENT_ENABLED:
		state = PCHG_STATE_ENABLED;
		break;
	default:
		break;
	}

	return state;
}

static enum pchg_state pchg_state_enabled(struct pchg *ctx)
{
	enum pchg_state state = PCHG_STATE_ENABLED;
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_DEVICE_DETECTED:
		state = PCHG_STATE_DETECTED;
		break;
	case PCHG_EVENT_CHARGE_STARTED:
		state = PCHG_STATE_CHARGING;
		break;
	default:
		break;
	}

	return state;
}

static enum pchg_state pchg_state_detected(struct pchg *ctx)
{
	enum pchg_state state = PCHG_STATE_DETECTED;
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_CHARGE_STARTED:
		state = PCHG_STATE_CHARGING;
		break;
	case PCHG_EVENT_DEVICE_LOST:
		state = PCHG_STATE_ENABLED;
		break;
	case PCHG_EVENT_CHARGE_ERROR:
		state = PCHG_STATE_INITIALIZED;
		break;
	default:
		break;
	}

	return state;
}

static enum pchg_state pchg_state_charging(struct pchg *ctx)
{
	enum pchg_state state = PCHG_STATE_CHARGING;
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_CHARGE_UPDATE:
		CPRINTS("Battery %d%%", ctx->battery_percent);
		break;
	case PCHG_EVENT_DEVICE_LOST:
		state = PCHG_STATE_ENABLED;
		break;
	case PCHG_EVENT_CHARGE_ERROR:
		state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_CHARGE_ENDED:
	case PCHG_EVENT_CHARGE_STOPPED:
		state = PCHG_STATE_DETECTED;
		break;
	default:
		break;
	}

	return state;
}

static int pchg_run(struct pchg *ctx)
{
	enum pchg_state previous_state = ctx->state;
	int port = PCHG_CTX_TO_PORT(ctx);
	int rv;

	mutex_lock(&ctx->mtx);
	if (!queue_remove_unit(&ctx->events, &ctx->event)) {
		mutex_unlock(&ctx->mtx);
		CPRINTS("P%d No event in queue", port);
		return 0;
	}
	mutex_unlock(&ctx->mtx);

	CPRINTS("P%d Run in STATE_%s for EVENT_%s", port,
		_text_state(ctx->state), _text_event(ctx->event));

	if (ctx->event == PCHG_EVENT_IRQ) {
		rv = ctx->cfg->drv->get_event(ctx);
		if (rv) {
			CPRINTS("ERR: get_event (%d)", rv);
			ctx->event = PCHG_EVENT_NONE;
			return 0;
		}
		CPRINTS("IRQ:EVENT_%s", _text_event(ctx->event));
	}

	switch (ctx->state) {
	case PCHG_STATE_RESET:
		ctx->state = pchg_state_reset(ctx);
		break;
	case PCHG_STATE_INITIALIZED:
		ctx->state = pchg_state_initialized(ctx);
		break;
	case PCHG_STATE_ENABLED:
		ctx->state = pchg_state_enabled(ctx);
		break;
	case PCHG_STATE_DETECTED:
		ctx->state = pchg_state_detected(ctx);
		break;
	case PCHG_STATE_CHARGING:
		ctx->state = pchg_state_charging(ctx);
		break;
	default:
		CPRINTS("ERR: Unknown state (%d)", ctx->state);
		return 0;
	}

	if (previous_state != ctx->state)
		CPRINTS("->%s", _text_state(ctx->state));

	ctx->event = PCHG_EVENT_NONE;
	CPRINTS("Done");

	return 1;
}

void pchg_irq(enum gpio_signal signal)
{
	struct pchg *ctx;
	int i;

	for (i = 0; i < pchg_count; i++) {
		ctx = &pchgs[i];
		if (signal == ctx->cfg->irq_pin) {
			ctx->irq = 1;
			task_wake(TASK_ID_PCHG);
			return;
		}
	}
}

void pchg_task(void *u)
{
	struct pchg *ctx;
	int p;
	int rv;

	/* TODO: i2c is wedged for a while after reset. investigate. */
	msleep(500);

	for (p = 0; p < pchg_count; p++) {
		ctx = &pchgs[p];
		ctx->state = PCHG_STATE_RESET;
		queue_init(&ctx->events);
		pchg_queue_event(ctx, PCHG_EVENT_INITIALIZE);
		gpio_enable_interrupt(ctx->cfg->irq_pin);
	}

	while (true) {
		/* Process pending events for all ports. */
		rv = 0;
		for (p = 0; p < pchg_count; p++) {
			ctx = &pchgs[p];
			do {
				if (atomic_clear(&ctx->irq))
					pchg_queue_event(ctx, PCHG_EVENT_IRQ);
				rv |= pchg_run(ctx);
			} while (queue_count(&ctx->events));
		}
		/*
		 * Send one host event for all ports. Currently PCHG supports
		 * only WLC but in the future other types (e.g. WPC Qi) should
		 * send different device events.
		 */
		if (rv)
			device_set_single_event(EC_DEVICE_EVENT_WLC);
		task_wait_event(-1);
	}
}

static enum ec_status hc_pchg_count(struct host_cmd_handler_args *args)
{
	struct ec_response_pchg_count *r = args->response;

	r->port_count = pchg_count;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PCHG_COUNT, hc_pchg_count, EC_VER_MASK(0));

static enum ec_status hc_pchg(struct host_cmd_handler_args *args)
{
	const struct ec_params_pchg *p = args->params;
	struct ec_response_pchg *r = args->response;
	int port = p->port;
	struct pchg *ctx;

	if (port >= pchg_count)
		return EC_RES_INVALID_PARAM;

	ctx = &pchgs[port];

	r->state = ctx->state;
	r->battery_percentage = ctx->battery_percent;
	r->error = ctx->error;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PCHG, hc_pchg, EC_VER_MASK(0));

static int cc_pchg(int argc, char **argv)
{
	int port;
	char *end;
	struct pchg *ctx;

	if (argc < 2 || 3 < argc)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &end, 0);
	if (*end || port < 0 || port >= pchg_count)
		return EC_ERROR_PARAM2;
	ctx = &pchgs[port];

	if (argc == 2) {
		ccprintf("P%d %s %s\n", port,
			 _text_state(ctx->state), _text_event(ctx->event));
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[2], "init")) {
		ctx->state = PCHG_STATE_RESET;
		pchg_queue_event(ctx, PCHG_EVENT_INITIALIZE);
	} else if (!strcasecmp(argv[2], "enable")) {
		pchg_queue_event(ctx, PCHG_EVENT_ENABLE);
	} else if (!strcasecmp(argv[2], "disable")) {
		pchg_queue_event(ctx, PCHG_EVENT_DISABLE);
	} else {
		return EC_ERROR_PARAM1;
	}

	task_wake(TASK_ID_PCHG);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pchg, cc_pchg,
			"<port> [init/enable/disable]"
			"\n\t<port>"
			"\n\t<port> init"
			"\n\t<port> enable"
			"\n\t<port> disable",
			"Control peripheral chargers");
