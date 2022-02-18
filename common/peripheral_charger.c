/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "chipset.h"
#include "common.h"
#include "device_event.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "peripheral_charger.h"
#include "queue.h"
#include "stdbool.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Peripheral Charge Manager */

#define CPRINTS(fmt, args...) cprints(CC_PCHG, "PCHG: " fmt, ##args)

/* Host event queue. Shared by all ports. */
static struct queue const host_events =
		QUEUE_NULL(PCHG_EVENT_QUEUE_SIZE, uint32_t);
struct mutex host_event_mtx;

static void pchg_queue_event(struct pchg *ctx, enum pchg_event event)
{
	mutex_lock(&ctx->mtx);
	if (queue_add_unit(&ctx->events, &event) == 0) {
		ctx->dropped_event_count++;
		CPRINTS("ERR: Queue is full");
	}
	mutex_unlock(&ctx->mtx);
}

static void pchg_queue_host_event(struct pchg *ctx, uint32_t event)
{
	size_t len;

	event |= EC_MKBP_PCHG_PORT_TO_EVENT(PCHG_CTX_TO_PORT(ctx));

	mutex_lock(&host_event_mtx);
	len = queue_add_unit(&host_events, &event);
	mutex_unlock(&host_event_mtx);
	if (len == 0) {
		ctx->dropped_host_event_count++;
		CPRINTS("ERR: Host event queue is full");
		/* Send a reminder. */
		mkbp_send_event(EC_MKBP_EVENT_PCHG);
		return;
	}

	mkbp_send_event(EC_MKBP_EVENT_PCHG);
}

static const char *_text_state(enum pchg_state state)
{
	/* TODO: Use "S%d" for normal build. */
	static const char * const state_names[] = EC_PCHG_STATE_TEXT;
	BUILD_ASSERT(ARRAY_SIZE(state_names) == PCHG_STATE_COUNT);

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
		[PCHG_EVENT_RESET] = "RESET",
		[PCHG_EVENT_INITIALIZED] = "INITIALIZED",
		[PCHG_EVENT_ENABLED] = "ENABLED",
		[PCHG_EVENT_DISABLED] = "DISABLED",
		[PCHG_EVENT_DEVICE_DETECTED] = "DEVICE_DETECTED",
		[PCHG_EVENT_DEVICE_CONNECTED] = "DEVICE_CONNECTED",
		[PCHG_EVENT_DEVICE_LOST] = "DEVICE_LOST",
		[PCHG_EVENT_CHARGE_STARTED] = "CHARGE_STARTED",
		[PCHG_EVENT_CHARGE_UPDATE] = "CHARGE_UPDATE",
		[PCHG_EVENT_CHARGE_ENDED] = "CHARGE_ENDED",
		[PCHG_EVENT_CHARGE_STOPPED] = "CHARGE_STOPPED",
		[PCHG_EVENT_UPDATE_OPENED] = "UPDATE_OPENED",
		[PCHG_EVENT_UPDATE_CLOSED] = "UPDATE_CLOSED",
		[PCHG_EVENT_UPDATE_WRITTEN] = "UPDATE_WRITTEN",
		[PCHG_EVENT_IN_NORMAL] = "IN_NORMAL",
		[PCHG_EVENT_CHARGE_ERROR] = "CHARGE_ERROR",
		[PCHG_EVENT_UPDATE_ERROR] = "UPDATE_ERROR",
		[PCHG_EVENT_OTHER_ERROR] = "OTHER_ERROR",
		[PCHG_EVENT_ENABLE] = "ENABLE",
		[PCHG_EVENT_DISABLE] = "DISABLE",
		[PCHG_EVENT_UPDATE_OPEN] = "UPDATE_OPEN",
		[PCHG_EVENT_UPDATE_WRITE] = "UPDATE_WRITE",
		[PCHG_EVENT_UPDATE_CLOSE] = "UPDATE_CLOSE",
	};
	BUILD_ASSERT(ARRAY_SIZE(event_names) == PCHG_EVENT_COUNT);

	if (event >= sizeof(event_names))
		return "UNDEF";

	return event_names[event];
}

static void _clear_port(struct pchg *ctx)
{
	mutex_lock(&ctx->mtx);
	queue_init(&ctx->events);
	mutex_unlock(&ctx->mtx);
	atomic_clear(&ctx->irq);
	ctx->battery_percent = 0;
	ctx->error = 0;
	ctx->update.data_ready = 0;
}

static enum pchg_state pchg_reset(struct pchg *ctx)
{
	enum pchg_state state = PCHG_STATE_RESET;
	int rv;

	/*
	 * In case we get asynchronous reset, clear port though it's redundant
	 * for a synchronous reset.
	 */
	_clear_port(ctx);

	if (ctx->mode == PCHG_MODE_NORMAL) {
		rv = ctx->cfg->drv->init(ctx);
		if (rv == EC_SUCCESS) {
			state = PCHG_STATE_INITIALIZED;
			pchg_queue_event(ctx, PCHG_EVENT_ENABLE);
		} else if (rv != EC_SUCCESS_IN_PROGRESS) {
			CPRINTS("ERR: Failed to reset to normal mode");
		}
	} else {
		state = PCHG_STATE_DOWNLOAD;
		pchg_queue_event(ctx, PCHG_EVENT_UPDATE_OPEN);
	}

	return state;
}

static void pchg_state_reset(struct pchg *ctx)
{
	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_IN_NORMAL:
		ctx->state = PCHG_STATE_INITIALIZED;
		pchg_queue_event(ctx, PCHG_EVENT_ENABLE);
		break;
	default:
		break;
	}
}

static void pchg_state_initialized(struct pchg *ctx)
{
	int rv;

	if (ctx->event == PCHG_EVENT_ENABLE)
		ctx->error &= ~PCHG_ERROR_HOST;

	/* Spin in INITIALIZED until error condition is cleared. */
	if (ctx->error)
		return;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_ENABLE:
		rv = ctx->cfg->drv->enable(ctx, true);
		if (rv == EC_SUCCESS)
			ctx->state = PCHG_STATE_ENABLED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to enable");
		break;
	case PCHG_EVENT_ENABLED:
		ctx->state = PCHG_STATE_ENABLED;
		break;
	default:
		break;
	}
}

static void pchg_state_enabled(struct pchg *ctx)
{
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			ctx->state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		ctx->state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_DEVICE_DETECTED:
		ctx->state = PCHG_STATE_DETECTED;
		break;
	case PCHG_EVENT_DEVICE_CONNECTED:
		/*
		 * Proactively query SOC in case charging info won't be sent
		 * because device is already charged.
		 */
		ctx->cfg->drv->get_soc(ctx);
		ctx->state = PCHG_STATE_CONNECTED;
		break;
	default:
		break;
	}
}

static void pchg_state_detected(struct pchg *ctx)
{
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			ctx->state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		ctx->state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_DEVICE_CONNECTED:
		/*
		 * Proactively query SOC in case charging info won't be sent
		 * because device is already charged.
		 */
		ctx->cfg->drv->get_soc(ctx);
		ctx->state = PCHG_STATE_CONNECTED;
		break;
	case PCHG_EVENT_DEVICE_LOST:
		ctx->battery_percent = 0;
		ctx->state = PCHG_STATE_ENABLED;
		break;
	default:
		break;
	}
}

static void pchg_state_connected(struct pchg *ctx)
{
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			ctx->state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		ctx->state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_CHARGE_STARTED:
		ctx->state = PCHG_STATE_CHARGING;
		break;
	case PCHG_EVENT_DEVICE_LOST:
		ctx->battery_percent = 0;
		ctx->state = PCHG_STATE_ENABLED;
		break;
	case PCHG_EVENT_CHARGE_ERROR:
		ctx->state = PCHG_STATE_INITIALIZED;
		break;
	default:
		break;
	}
}

static void pchg_state_charging(struct pchg *ctx)
{
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_DISABLE:
		ctx->error |= PCHG_ERROR_HOST;
		rv = ctx->cfg->drv->enable(ctx, false);
		if (rv == EC_SUCCESS)
			ctx->state = PCHG_STATE_INITIALIZED;
		else if (rv != EC_SUCCESS_IN_PROGRESS)
			CPRINTS("ERR: Failed to disable");
		break;
	case PCHG_EVENT_DISABLED:
		ctx->state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_CHARGE_UPDATE:
		break;
	case PCHG_EVENT_DEVICE_LOST:
		ctx->battery_percent = 0;
		ctx->state = PCHG_STATE_ENABLED;
		break;
	case PCHG_EVENT_CHARGE_ERROR:
		ctx->state = PCHG_STATE_INITIALIZED;
		break;
	case PCHG_EVENT_CHARGE_ENDED:
	case PCHG_EVENT_CHARGE_STOPPED:
		ctx->state = PCHG_STATE_CONNECTED;
		break;
	default:
		break;
	}
}

static void pchg_state_download(struct pchg *ctx)
{
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_UPDATE_OPEN:
		rv = ctx->cfg->drv->update_open(ctx);
		if (rv == EC_SUCCESS) {
			ctx->state = PCHG_STATE_DOWNLOADING;
		} else if (rv != EC_SUCCESS_IN_PROGRESS) {
			pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_ERROR);
			CPRINTS("ERR: Failed to open");
		}
		break;
	case PCHG_EVENT_UPDATE_OPENED:
		ctx->state = PCHG_STATE_DOWNLOADING;
		pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_OPENED);
		break;
	case PCHG_EVENT_UPDATE_ERROR:
		pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_ERROR);
		break;
	default:
		break;
	}
}

static void pchg_state_downloading(struct pchg *ctx)
{
	int rv;

	switch (ctx->event) {
	case PCHG_EVENT_RESET:
		ctx->state = pchg_reset(ctx);
		break;
	case PCHG_EVENT_UPDATE_WRITE:
		if (ctx->update.data_ready == 0)
			break;
		rv = ctx->cfg->drv->update_write(ctx);
		if (rv != EC_SUCCESS && rv != EC_SUCCESS_IN_PROGRESS) {
			pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_ERROR);
			CPRINTS("ERR: Failed to write");
		}
		break;
	case PCHG_EVENT_UPDATE_WRITTEN:
		ctx->update.data_ready = 0;
		pchg_queue_host_event(ctx, EC_MKBP_PCHG_WRITE_COMPLETE);
		break;
	case PCHG_EVENT_UPDATE_CLOSE:
		rv = ctx->cfg->drv->update_close(ctx);
		if (rv == EC_SUCCESS) {
			ctx->state = PCHG_STATE_DOWNLOAD;
		} else if (rv != EC_SUCCESS_IN_PROGRESS) {
			pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_ERROR);
			CPRINTS("ERR: Failed to close");
		}
		break;
	case PCHG_EVENT_UPDATE_CLOSED:
		ctx->state = PCHG_STATE_DOWNLOAD;
		pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_CLOSED);
		break;
	case PCHG_EVENT_UPDATE_ERROR:
		CPRINTS("ERR: Failed to update");
		pchg_queue_host_event(ctx, EC_MKBP_PCHG_UPDATE_ERROR);
		break;
	default:
		break;
	}
}

/**
 * Process an event.
 *
 * The handler of the current state processes one event. If the event is IRQ,
 * the driver is called (get_event), which translates the event to an actual
 * event. Note that state handlers themselves may enqueue a new event.
 *
 * It returns 1 if the processed event needs to be reported to the host. This is
 * notified as EC_MKBP_PCHG_DEVICE_EVENT. The host will call EC_CMD_PCHG to get
 * updated status including the SoC and errors.
 *
 * State handlers may send a host event separately. For example, FW update
 * events are reported as EC_MKBP_PCHG_UPDATE_*.
 *
 * @param ctx
 * @return 1: Notify host of EC_MKBP_PCHG_DEVICE_EVENT.
 */
static int pchg_run(struct pchg *ctx)
{
	enum pchg_state previous_state = ctx->state;
	uint8_t previous_battery = ctx->battery_percent;
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
			CPRINTS("ERR: Failed to get event (%d)", rv);
			return 0;
		}
		CPRINTS("  EVENT_%s", _text_event(ctx->event));
	}

	if (ctx->event == PCHG_EVENT_NONE)
		return 0;

	switch (ctx->state) {
	case PCHG_STATE_RESET:
		pchg_state_reset(ctx);
		break;
	case PCHG_STATE_INITIALIZED:
		pchg_state_initialized(ctx);
		break;
	case PCHG_STATE_ENABLED:
		pchg_state_enabled(ctx);
		break;
	case PCHG_STATE_DETECTED:
		pchg_state_detected(ctx);
		break;
	case PCHG_STATE_CONNECTED:
		pchg_state_connected(ctx);
		break;
	case PCHG_STATE_CHARGING:
		pchg_state_charging(ctx);
		break;
	case PCHG_STATE_DOWNLOAD:
		pchg_state_download(ctx);
		break;
	case PCHG_STATE_DOWNLOADING:
		pchg_state_downloading(ctx);
		break;
	default:
		CPRINTS("ERR: Unknown state (%d)", ctx->state);
		return 0;
	}

	if (previous_state != ctx->state)
		CPRINTS("->STATE_%s", _text_state(ctx->state));

	if (ctx->battery_percent != previous_battery)
		CPRINTS("Battery %u%%", ctx->battery_percent);

	/*
	 * Notify the host of
	 * - [S0] Charge update with SoC change and all other events except
	 *   FW update events.
	 * - [S3/S0IX] Device attach or detach (for wake-up)
	 * - [S5/G3] No events.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return 0;

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		return (ctx->event == PCHG_EVENT_DEVICE_DETECTED)
			|| (ctx->event == PCHG_EVENT_DEVICE_LOST);

	if (ctx->event == PCHG_EVENT_CHARGE_UPDATE)
		return ctx->battery_percent != previous_battery;

	/* Don't report FW update events because they're separately reported. */
	if (ctx->event == PCHG_EVENT_UPDATE_OPENED ||
	    ctx->event == PCHG_EVENT_UPDATE_CLOSED ||
	    ctx->event == PCHG_EVENT_UPDATE_WRITTEN ||
	    ctx->event == PCHG_EVENT_UPDATE_ERROR ||
	    ctx->event == PCHG_EVENT_UPDATE_OPEN ||
	    ctx->event == PCHG_EVENT_UPDATE_WRITE ||
	    ctx->event == PCHG_EVENT_UPDATE_CLOSE)
		return 0;

	return ctx->event != PCHG_EVENT_NONE;
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


static void pchg_suspend_complete(void)
{
	CPRINTS("%s", __func__);
	device_enable_event(EC_DEVICE_EVENT_WLC);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND_COMPLETE, pchg_suspend_complete,
	     HOOK_PRIO_DEFAULT);

static void pchg_startup(void)
{
	struct pchg *ctx;
	int p;

	CPRINTS("%s", __func__);
	queue_init(&host_events);

	for (p = 0; p < pchg_count; p++) {
		ctx = &pchgs[p];
		_clear_port(ctx);
		ctx->mode = PCHG_MODE_NORMAL;
		ctx->cfg->drv->reset(ctx);
		gpio_enable_interrupt(ctx->cfg->irq_pin);
	}

	task_wake(TASK_ID_PCHG);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pchg_startup, HOOK_PRIO_DEFAULT);

static void pchg_shutdown(void)
{
	struct pchg *ctx;
	int p;

	CPRINTS("%s", __func__);

	for (p = 0; p < pchg_count; p++) {
		ctx = &pchgs[0];
		gpio_disable_interrupt(ctx->cfg->irq_pin);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pchg_shutdown, HOOK_PRIO_DEFAULT);

void pchg_task(void *u)
{
	struct pchg *ctx;
	int p;

	if (chipset_in_state(CHIPSET_STATE_ON))
		/* We are here after power-on (because of late sysjump). */
		pchg_startup();

	while (true) {
		/* Process pending events for all ports. */
		for (p = 0; p < pchg_count; p++) {
			ctx = &pchgs[p];
			do {
				if (atomic_clear(&ctx->irq))
					pchg_queue_event(ctx, PCHG_EVENT_IRQ);
				if (pchg_run(ctx))
					pchg_queue_host_event(
						ctx, EC_MKBP_PCHG_DEVICE_EVENT);
			} while (queue_count(&ctx->events));
		}

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

#define HCPRINTS(fmt, args...) cprints(CC_PCHG, "HC:PCHG: " fmt, ##args)

static enum ec_status hc_pchg(struct host_cmd_handler_args *args)
{
	const struct ec_params_pchg *p = args->params;
	struct ec_response_pchg_v2 *r = args->response;
	int port = p->port;
	struct pchg *ctx;

	/* Version 0 shouldn't exist. */
	if (args->version == 0)
		return EC_RES_INVALID_VERSION;

	if (port >= pchg_count)
		return EC_RES_INVALID_PARAM;

	ctx = &pchgs[port];

	if (ctx->state == PCHG_STATE_CONNECTED
			&& ctx->battery_percent >= ctx->cfg->full_percent)
		r->state = PCHG_STATE_FULL;
	else
		r->state = ctx->state;

	r->battery_percentage = ctx->battery_percent;
	r->error = ctx->error;
	r->fw_version = ctx->fw_version;
	r->dropped_event_count = ctx->dropped_event_count;
	r->dropped_host_event_count = ctx->dropped_host_event_count;

	args->response_size = args->version == 1 ?
			sizeof(struct ec_response_pchg) : sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PCHG, hc_pchg, EC_VER_MASK(1) | EC_VER_MASK(2));

int pchg_get_next_event(uint8_t *out)
{
	uint32_t event;
	size_t len;

	mutex_lock(&host_event_mtx);
	len = queue_remove_unit(&host_events, &event);
	mutex_unlock(&host_event_mtx);
	if (len == 0)
		return 0;

	memcpy(out, &event, sizeof(event));

	/* Ping host again if there are more events to send. */
	if (queue_count(&host_events))
		mkbp_send_event(EC_MKBP_EVENT_PCHG);

	return sizeof(event);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_PCHG, pchg_get_next_event);

static enum ec_status hc_pchg_update(struct host_cmd_handler_args *args)
{
	const struct ec_params_pchg_update *p = args->params;
	struct ec_response_pchg_update *r = args->response;
	int port = p->port;
	struct pchg *ctx;

	if (port >= pchg_count)
		return EC_RES_INVALID_PARAM;

	ctx = &pchgs[port];

	switch (p->cmd) {
	case EC_PCHG_UPDATE_CMD_RESET_TO_NORMAL:
		HCPRINTS("Resetting to normal mode");

		gpio_disable_interrupt(ctx->cfg->irq_pin);
		_clear_port(ctx);
		ctx->mode = PCHG_MODE_NORMAL;
		ctx->cfg->drv->reset(ctx);
		gpio_enable_interrupt(ctx->cfg->irq_pin);
		break;

	case EC_PCHG_UPDATE_CMD_OPEN:
		HCPRINTS("Resetting to download mode");

		gpio_disable_interrupt(ctx->cfg->irq_pin);
		_clear_port(ctx);
		ctx->mode = PCHG_MODE_DOWNLOAD;
		ctx->cfg->drv->reset(ctx);
		gpio_enable_interrupt(ctx->cfg->irq_pin);

		ctx->update.version = p->version;
		r->block_size = ctx->cfg->block_size;
		args->response_size = sizeof(*r);
		break;

	case EC_PCHG_UPDATE_CMD_WRITE:
		if (ctx->state != PCHG_STATE_DOWNLOADING)
			return EC_RES_ERROR;
		if (p->size > sizeof(ctx->update.data))
			return EC_RES_OVERFLOW;
		if (ctx->update.data_ready)
			return EC_RES_BUSY;

		HCPRINTS("Writing %u bytes to 0x%x", p->size, p->addr);
		ctx->update.addr = p->addr;
		ctx->update.size = p->size;
		memcpy(ctx->update.data, p->data, p->size);
		pchg_queue_event(ctx, PCHG_EVENT_UPDATE_WRITE);
		ctx->update.data_ready = 1;
		break;

	case EC_PCHG_UPDATE_CMD_CLOSE:
		if (ctx->state != PCHG_STATE_DOWNLOADING)
			return EC_RES_ERROR;
		if (ctx->update.data_ready)
			return EC_RES_BUSY;

		HCPRINTS("Closing update session (crc=0x%x)", p->crc32);
		ctx->update.crc32 = p->crc32;
		pchg_queue_event(ctx, PCHG_EVENT_UPDATE_CLOSE);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	task_wake(TASK_ID_PCHG);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PCHG_UPDATE, hc_pchg_update, EC_VER_MASK(0));

static int cc_pchg(int argc, char **argv)
{
	int port;
	char *end;
	struct pchg *ctx;

	if (argc < 2 || 4 < argc)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &end, 0);
	if (*end || port < 0 || port >= pchg_count)
		return EC_ERROR_PARAM1;
	ctx = &pchgs[port];

	if (argc == 2) {
		ccprintf("P%d STATE_%s EVENT_%s SOC=%d%%\n",
			 port, _text_state(ctx->state), _text_event(ctx->event),
			 ctx->battery_percent);
		ccprintf("error=0x%x dropped=%u fw_version=0x%x\n",
			 ctx->error, ctx->dropped_event_count, ctx->fw_version);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[2], "reset")) {
		if (argc == 3)
			ctx->mode = PCHG_MODE_NORMAL;
		else if (!strcasecmp(argv[3], "download"))
			ctx->mode = PCHG_MODE_DOWNLOAD;
		else
			return EC_ERROR_PARAM3;
		gpio_disable_interrupt(ctx->cfg->irq_pin);
		_clear_port(ctx);
		ctx->cfg->drv->reset(ctx);
		gpio_enable_interrupt(ctx->cfg->irq_pin);
	} else if (!strcasecmp(argv[2], "enable")) {
		pchg_queue_event(ctx, PCHG_EVENT_ENABLE);
	} else if (!strcasecmp(argv[2], "disable")) {
		pchg_queue_event(ctx, PCHG_EVENT_DISABLE);
	} else {
		return EC_ERROR_PARAM2;
	}

	task_wake(TASK_ID_PCHG);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pchg, cc_pchg,
			"\n\t<port>"
			"\n\t<port> reset [download]"
			"\n\t<port> enable"
			"\n\t<port> disable",
			"Control peripheral chargers");
