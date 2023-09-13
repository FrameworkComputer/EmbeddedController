/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Source file for PD task to configure USB-C Alternate modes on Intel SoC.
 */

#include "i2c.h"
#include "i2c/i2c.h"
#include "usbc/utils.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <ap_power/ap_power.h>
#include <drivers/intel_altmode.h>
#include <usbc/pd_task_intel_altmode.h>

LOG_MODULE_DECLARE(usbpd_altmode, CONFIG_USB_PD_ALTMODE_LOG_LEVEL);

#define INTEL_ALTMODE_COMPAT_PD intel_pd_altmode

#define PD_CHIP_ENTRY(usbc_id, pd_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(pd_id),

#define CHECK_COMPAT(compat, usbc_id, pd_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(pd_id, compat),  \
		    (PD_CHIP_ENTRY(usbc_id, pd_id, config_fn)), ())

#define PD_CHIP_FIND(usbc_id, pd_id) \
	CHECK_COMPAT(INTEL_ALTMODE_COMPAT_PD, usbc_id, pd_id, DEVICE_DT_GET)

#define PD_CHIP(usbc_id)                                                    \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, alt_mode),                    \
		    (PD_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, alt_mode))), \
		    ())

#define INTEL_ALTMODE_EVENT_MASK GENMASK(INTEL_ALTMODE_EVENT_COUNT - 1, 0)

enum intel_altmode_event {
	INTEL_ALTMODE_EVENT_FORCE,
	INTEL_ALTMODE_EVENT_INTERRUPT,
	INTEL_ALTMODE_EVENT_COUNT
};

struct intel_altmode_data {
	/* Driver event object to receive events posted. */
	struct k_event evt;
	/* Callback for the AP power events */
	struct ap_power_ev_callback cb;
	/* Cache the dta status register */
	union data_status_reg data_status[CONFIG_USB_PD_PORT_MAX_COUNT];
};

/* Generate device tree for available PDs */
static const struct device *pd_config_array[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, PD_CHIP) };

BUILD_ASSERT(ARRAY_SIZE(pd_config_array) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* Store the task data */
static struct intel_altmode_data intel_altmode_task_data;

static void intel_altmode_post_event(enum intel_altmode_event event)
{
	k_event_post(&intel_altmode_task_data.evt, BIT(event));
}

static void intel_altmode_suspend_handler(struct ap_power_ev_callback *cb,
					  struct ap_power_ev_data data)
{
	LOG_DBG("suspend event: 0x%x", data.event);

	if (data.event == AP_POWER_RESUME) {
		/*
		 * Set event to forcefully get new PD data.
		 * This ensures EC doesn't miss the interrupt if the interrupt
		 * pull-ups are on A-rail.
		 */
		intel_altmode_post_event(INTEL_ALTMODE_EVENT_FORCE);
	} else {
		LOG_ERR("Invalid suspend event");
	}
}

static void intel_altmode_event_cb(void)
{
	intel_altmode_post_event(INTEL_ALTMODE_EVENT_INTERRUPT);
}

static uint32_t intel_altmode_wait_event(void)
{
	uint32_t events;

	events = k_event_wait(&intel_altmode_task_data.evt,
			      INTEL_ALTMODE_EVENT_MASK, false, Z_FOREVER);

	/* Clear all events posted */
	k_event_clear(&intel_altmode_task_data.evt, events);

	return events & INTEL_ALTMODE_EVENT_MASK;
}

static void process_altmode_pd_data(int port)
{
	int rv;
	union data_status_reg status;
	union data_status_reg *prev_status =
		&intel_altmode_task_data.data_status[port];
	union data_control_reg control = { .i2c_int_ack = 1 };

	LOG_INF("Process p%d data", port);

	/* Clear the interrupt */
	rv = pd_altmode_write(pd_config_array[port], &control);
	if (rv) {
		LOG_ERR("P%d write Err=%d", port, rv);
		return;
	}

	/* Read the status register */
	rv = pd_altmode_read(pd_config_array[port], &status);
	if (rv) {
		LOG_ERR("P%d read Err=%d", port, rv);
		return;
	}

	/* Nothing to do if the data in the status register has not changed */
	if (!memcmp(&status.raw_value[0], prev_status,
		    sizeof(union data_status_reg)))
		return;

	/* Update the new data */
	memcpy(prev_status, &status, sizeof(union data_status_reg));

	/* TODO: Process MUX events */
}

static void intel_altmode_thread(void *unused1, void *unused2, void *unused3)
{
	int i;
	uint32_t events;

	/* Initialize events */
	k_event_init(&intel_altmode_task_data.evt);

	/* Add callbacks for suspend hooks */
	ap_power_ev_init_callback(&intel_altmode_task_data.cb,
				  intel_altmode_suspend_handler,
				  AP_POWER_RESUME);
	ap_power_ev_add_callback(&intel_altmode_task_data.cb);

	/* Register PD interrupt callback */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		pd_altmode_set_result_cb(pd_config_array[i],
					 intel_altmode_event_cb);

	LOG_INF("Intel Altmode thread start");

	while (1) {
		events = intel_altmode_wait_event();

		LOG_DBG("Altmode events=0x%x", events);

		if (events & BIT(INTEL_ALTMODE_EVENT_INTERRUPT)) {
			/* Process data of interrupted port */
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
				if (pd_altmode_is_interrupted(
					    pd_config_array[i]))
					process_altmode_pd_data(i);
			}
		} else if (events & BIT(INTEL_ALTMODE_EVENT_FORCE)) {
			/* Process data for any wake events on all ports */
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
				process_altmode_pd_data(i);
		}
	}
}

K_THREAD_DEFINE(intel_altmode_tid, CONFIG_TASK_PD_ALTMODE_INTEL_STACK_SIZE,
		intel_altmode_thread, NULL, NULL, NULL,
		CONFIG_USBPD_ALTMODE_INTEL_THREAD_PRIORITY, 0, K_TICKS_FOREVER);

void intel_altmode_task_start(void)
{
	k_thread_start(intel_altmode_tid);
}
