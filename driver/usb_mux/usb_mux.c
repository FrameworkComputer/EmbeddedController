/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux high-level driver. */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "chipset.h"
#include "hooks.h"
#include "host_command.h"
#include "queue.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

static int enable_debug_prints;

/*
 * Flags will reset to 0 after sysjump; This works for current flags as LPM will
 * get reset in the init method which is called during PD task startup.
 */
static uint32_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Device is in low power mode. */
#define USB_MUX_FLAG_IN_LPM		BIT(0)

/* Device initialized at least once */
#define USB_MUX_FLAG_INIT		BIT(1)

/* Coordinate mux accesses by-port among the tasks */
static mutex_t mux_lock[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Coordinate which task requires an ACK event */
static task_id_t ack_task[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ... CONFIG_USB_PD_PORT_MAX_COUNT - 1] = TASK_ID_INVALID };

static void perform_mux_set(int port, mux_state_t mux_mode,
			    enum usb_switch usb_mode, int polarity);
static void perform_mux_hpd_update(int port, mux_state_t hpd_state);

enum mux_config_type {
	USB_MUX_INIT,
	USB_MUX_LOW_POWER,
	USB_MUX_SET_MODE,
	USB_MUX_GET_MODE,
	USB_MUX_CHIPSET_RESET,
	USB_MUX_HPD_UPDATE,
};

/* Define a USB mux task ID for the purpose of linking */
#ifndef HAS_TASK_USB_MUX
#define TASK_ID_USB_MUX TASK_ID_INVALID
#endif

/*
 * USB mux task
 *
 * Since USB mux sets can take extended periods of time (on the order of 100s of
 * ms for some muxes), run a small task to complete those mux sets in order to
 * not block the PD task.  Run HPD sets from this task as well, since they
 * should be sequenced behind setting up the mux pins for DP.
 *
 * Depth must be a power of 2, which is normally enforced by the queue init
 * code, but must be manually enforced here.
 */
#define MUX_QUEUE_DEPTH		4
BUILD_ASSERT(POWER_OF_TWO(MUX_QUEUE_DEPTH));

/* Define in order to enable debug info about how long the queue takes */
#undef DEBUG_MUX_QUEUE_TIME

struct mux_queue_entry {
	enum mux_config_type type;
	mux_state_t mux_mode;		/* For both HPD and mux set */
	enum usb_switch usb_config;	/* Set only */
	int polarity;			/* Set only */
#ifdef DEBUG_MUX_QUEUE_TIME
	timestamp_t enqueued_time;
#endif
};

/*
 * Note: test builds won't optimize out the mux task code and thereby require
 * the queue to link
 */
#if defined(TEST_BUILD) || defined(HAS_TASK_USB_MUX)
/*
 * Note: QUEUE macros cannot be used to initialize this array, since they rely
 * on anonymous data structs for allocation which results in all entries
 * sharing the same state pointer and data buffers.
 */
static struct queue mux_queue[CONFIG_USB_PD_PORT_MAX_COUNT];
__maybe_unused static struct queue_state
				queue_states[CONFIG_USB_PD_PORT_MAX_COUNT];
__maybe_unused static struct mux_queue_entry
				queue_buffers[CONFIG_USB_PD_PORT_MAX_COUNT]
					     [MUX_QUEUE_DEPTH];
static mutex_t queue_lock[CONFIG_USB_PD_PORT_MAX_COUNT];
#else
extern struct queue const mux_queue[];
extern mutex_t queue_lock[];
#endif

#ifdef CONFIG_ZEPHYR
static int init_mux_mutex(const struct device *dev)
{
	int port;

	ARG_UNUSED(dev);
	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		k_mutex_init(&mux_lock[port]);

		if (IS_ENABLED(HAS_TASK_USB_MUX))
			k_mutex_init(&queue_lock[port]);
	}

	return 0;
}
SYS_INIT(init_mux_mutex, POST_KERNEL, 50);
#endif /* CONFIG_ZEPHYR */

__maybe_unused static void mux_task_enqueue(int port, enum mux_config_type type,
					    mux_state_t mux_mode,
					    enum usb_switch usb_config,
					    int polarity)
{
	struct mux_queue_entry new_entry;

	if (!IS_ENABLED(HAS_TASK_USB_MUX))
		return;

	new_entry.type = type;
	new_entry.mux_mode = mux_mode;
	new_entry.usb_config = usb_config;
	new_entry.polarity = polarity;
#ifdef DEBUG_MUX_QUEUE_TIME
	new_entry.enqueued_time = get_time();
#endif

	mutex_lock(&queue_lock[port]);

	if (queue_add_unit(&mux_queue[port], &new_entry) == 0)
		CPRINTS("Error: Dropping port %d mux %d", port, type);
	else
		task_wake(TASK_ID_USB_MUX);

	mutex_unlock(&queue_lock[port]);
}

#ifdef HAS_TASK_USB_MUX
static void init_queue_structs(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		mux_queue[i].state = &queue_states[i];
		mux_queue[i].policy = &queue_policy_null;
		mux_queue[i].buffer_units = MUX_QUEUE_DEPTH;
		mux_queue[i].buffer_units_mask = MUX_QUEUE_DEPTH - 1;
		mux_queue[i].unit_bytes = sizeof(struct mux_queue_entry);
		mux_queue[i].buffer = (uint8_t *) &queue_buffers[i][0];
	}
}
DECLARE_HOOK(HOOK_INIT, init_queue_structs, HOOK_PRIO_FIRST);
#endif

__maybe_unused void usb_mux_task(void *u)
{
	bool items_waiting = true;

	while (1) {
		int port;

		/* Wait if we had no queue items to service */
		if (!items_waiting)
			task_wait_event(-1);

		items_waiting = false;

		/*
		 * Round robin the ports, so no one port can monopolize the task
		 */
		for (port = 0; port < board_get_usb_pd_port_count(); port++) {
			if (queue_count(&mux_queue[port])) {
				/*
				 * Process our first item.  Leave it in the
				 * queue until we've completed its operation so
				 * the PD task can tell it is still pending.
				 * Note this should be safe to do unlocked, as
				 * this task is the only one which changes the
				 * queue head.
				 */
				struct mux_queue_entry next;

				queue_peek_units(&mux_queue[port], &next, 0, 1);

#ifdef DEBUG_MUX_QUEUE_TIME
				CPRINTS("C%d: Start mux set queued %d us ago",
					port, time_since32(next.enqueued_time));
#endif
				if (next.type == USB_MUX_SET_MODE)
					perform_mux_set(port, next.mux_mode,
							next.usb_config,
							next.polarity);
				else if (next.type == USB_MUX_HPD_UPDATE)
					perform_mux_hpd_update(port,
							       next.mux_mode);
				else
					CPRINTS("Error: Unknown mux task type:"
						"%d", next.type);

#ifdef DEBUG_MUX_QUEUE_TIME
				CPRINTS("C%d: Completed mux set queued %d "
					"us ago",
					port, time_since32(next.enqueued_time));
#endif
				/*
				 * Lock since the tail is changing, which would
				 * disrupt any calls iterating the queue.
				 */
				mutex_lock(&queue_lock[port]);
				queue_advance_head(&mux_queue[port], 1);
				mutex_unlock(&queue_lock[port]);

				/*
				 * Force the task to run again if this queue has
				 * more items to process.
				 */
				if (queue_count(&mux_queue[port]))
					items_waiting = true;
			}
		}
	}
}

/* Configure the MUX */
static int configure_mux(int port,
			 enum mux_config_type config,
			 mux_state_t *mux_state)
{
	int rv = EC_SUCCESS;
	const struct usb_mux *mux_ptr;

	if (config == USB_MUX_SET_MODE ||
	    config == USB_MUX_GET_MODE) {
		if (mux_state == NULL)
			return EC_ERROR_INVAL;

		if (config == USB_MUX_GET_MODE)
			*mux_state = USB_PD_MUX_NONE;
	}

	/*
	 * a MUX for a particular port can be a linked list chain of
	 * MUXes.  So when we change one, we traverse the whole list
	 * to make sure they are all updated appropriately.
	 */
	for (mux_ptr = &usb_muxes[port];
	     rv == EC_SUCCESS && mux_ptr != NULL;
	     mux_ptr = mux_ptr->next_mux) {
		mux_state_t lcl_state;
		const struct usb_mux_driver *drv = mux_ptr->driver;
		bool ack_required = false;

		/* Action time!  Lock this mux */
		mutex_lock(&mux_lock[port]);

		switch (config) {
		case USB_MUX_INIT:
			if (drv && drv->init) {
				rv = drv->init(mux_ptr);
				if (rv)
					break;
			}

			/* Apply board specific initialization */
			if (mux_ptr->board_init)
				rv = mux_ptr->board_init(mux_ptr);

			break;

		case USB_MUX_LOW_POWER:
			if (drv && drv->enter_low_power_mode)
				rv = drv->enter_low_power_mode(mux_ptr);

			break;

		case USB_MUX_CHIPSET_RESET:
			if (drv && drv->chipset_reset)
				rv = drv->chipset_reset(mux_ptr);

			break;

		case USB_MUX_SET_MODE:
			lcl_state = *mux_state;

			if (mux_ptr->flags & USB_MUX_FLAG_SET_WITHOUT_FLIP)
				lcl_state &= ~USB_PD_MUX_POLARITY_INVERTED;

			if (drv && drv->set) {
				rv = drv->set(mux_ptr, lcl_state,
					      &ack_required);
				if (rv)
					break;
			}

			if (ack_required)
				ack_task[port] = task_get_current();

			/* Apply board specific setting */
			if (mux_ptr->board_set)
				rv = mux_ptr->board_set(mux_ptr, lcl_state);

			break;

		case USB_MUX_GET_MODE:
			/*
			 * This is doing a GET_CC on all of the MUXes in the
			 * chain and ORing them together. This will make sure
			 * if one of the MUX values has FLIP turned off that
			 * we will end up with the correct value in the end.
			 */
			if (drv && drv->get) {
				rv = drv->get(mux_ptr, &lcl_state);
				if (rv)
					break;
				*mux_state |= lcl_state;
			}
			break;

		case USB_MUX_HPD_UPDATE:
			if (mux_ptr->hpd_update)
				mux_ptr->hpd_update(mux_ptr, *mux_state);

		}

		/* Unlock before any host command waits */
		mutex_unlock(&mux_lock[port]);

		if (ack_required) {
			/*
			 * This should only be called from the PD task or usb
			 * mux task
			 */
			if (IS_ENABLED(HAS_TASK_USB_MUX)) {
				assert(task_get_current() == TASK_ID_USB_MUX);
			} else {
				assert(port ==
				       TASK_ID_TO_PD_PORT(task_get_current()));
			}

			/*
			 * Note: This task event could be generalized for more
			 * purposes beyond host command ACKs.  For now, these
			 * wait times are tuned for the purposes of the TCSS
			 * mux, but could be made configurable for other
			 * purposes.
			 */
			task_wait_event_mask(PD_EVENT_AP_MUX_DONE, 100*MSEC);
			ack_task[port] = TASK_ID_INVALID;

			usleep(12.5 * MSEC);
		}
	}

	if (rv)
		CPRINTS("mux config:%d, port:%d, rv:%d",
			config, port, rv);

	return rv;
}

static void enter_low_power_mode(int port)
{
	/*
	 * Set LPM flag regardless of method presence or method failure. We
	 * want know know that we tried to put the device in low power mode
	 * so we can re-initialize the device on the next access.
	 */
	atomic_or(&flags[port], USB_MUX_FLAG_IN_LPM);

	/* Apply any low power customization if present */
	configure_mux(port, USB_MUX_LOW_POWER, NULL);
}

static int exit_low_power_mode(int port)
{
	/* If we are in low power, initialize device (which clears LPM flag) */
	if (flags[port] & USB_MUX_FLAG_IN_LPM)
		usb_mux_init(port);

	if (!(flags[port] & USB_MUX_FLAG_INIT)) {
		CPRINTS("C%d: USB_MUX_FLAG_INIT not set", port);
		return EC_ERROR_UNKNOWN;
	}

	if (flags[port] & USB_MUX_FLAG_IN_LPM) {
		CPRINTS("C%d: USB_MUX_FLAG_IN_LPM not cleared", port);
		return EC_ERROR_NOT_POWERED;
	}

	return EC_SUCCESS;
}

void usb_mux_init(int port)
{
	int rv;

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	if (port >= board_get_usb_pd_port_count()) {
		return;
	}

	rv = configure_mux(port, USB_MUX_INIT, NULL);

	if (rv == EC_SUCCESS)
		atomic_or(&flags[port], USB_MUX_FLAG_INIT);

	/*
	 * Mux may fail initialization if it's not powered. Mark this port
	 * as in LPM mode to try initialization again.
	 */
	if (rv == EC_ERROR_NOT_POWERED)
		atomic_or(&flags[port], USB_MUX_FLAG_IN_LPM);
	else
		atomic_clear_bits(&flags[port], USB_MUX_FLAG_IN_LPM);
}

static void perform_mux_set(int port, mux_state_t mux_mode,
			    enum usb_switch usb_mode, int polarity)
{
	mux_state_t mux_state;
	const int should_enter_low_power_mode =
		(mux_mode == USB_PD_MUX_NONE &&
		usb_mode == USB_SWITCH_DISCONNECT);

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

	/* Configure USB2.0 */
	if (IS_ENABLED(CONFIG_USB_CHARGER))
		usb_charger_set_switches(port, usb_mode);

	/*
	 * Don't wake device up just to put it back to sleep. Low power mode
	 * flag is only set if the mux set() operation succeeded previously for
	 * the same disconnected state.
	 */
	if (should_enter_low_power_mode && (flags[port] & USB_MUX_FLAG_IN_LPM))
		return;

	if (exit_low_power_mode(port) != EC_SUCCESS)
		return;

	/* Configure superspeed lanes */
	mux_state = ((mux_mode != USB_PD_MUX_NONE) && polarity)
			? mux_mode | USB_PD_MUX_POLARITY_INVERTED
			: mux_mode;

	if (configure_mux(port, USB_MUX_SET_MODE, &mux_state))
		return;

	if (enable_debug_prints)
		CPRINTS(
		     "usb/dp mux: port(%d) typec_mux(%d) usb2(%d) polarity(%d)",
		     port, mux_mode, usb_mode, polarity);

	/*
	 * If we are completely disconnecting the mux, then we should put it in
	 * its lowest power state.
	 */
	if (should_enter_low_power_mode)
		enter_low_power_mode(port);
}

void usb_mux_set(int port, mux_state_t mux_mode,
		 enum usb_switch usb_mode, int polarity)
{
	if (port >= board_get_usb_pd_port_count())
		return;

	/* Block if we have no mux task, but otherwise queue it up and return */
	if (IS_ENABLED(HAS_TASK_USB_MUX))
		mux_task_enqueue(port, USB_MUX_SET_MODE, mux_mode,
				 usb_mode, polarity);
	else
		perform_mux_set(port,  mux_mode, usb_mode, polarity);
}

bool usb_mux_set_completed(int port)
{
	bool sets_pending = false;
	struct queue_iterator it;

	/* No mux task, no items waiting to process */
	if (!IS_ENABLED(HAS_TASK_USB_MUX))
		return true;

	/* Lock the queue so we can scroll through the items left to do */
	mutex_lock(&queue_lock[port]);

	for (queue_begin(&mux_queue[port], &it); it.ptr != NULL;
					queue_next(&mux_queue[port], &it)) {
		const struct mux_queue_entry *check =
					(struct mux_queue_entry *) it.ptr;

		if (check->type == USB_MUX_SET_MODE) {
			sets_pending = true;
			break;
		}
	}

	mutex_unlock(&queue_lock[port]);

	return !sets_pending;
}

mux_state_t usb_mux_get(int port)
{
	mux_state_t mux_state;
	int rv;

	if (port >= board_get_usb_pd_port_count()) {
		return USB_PD_MUX_NONE;
	}

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

	if (flags[port] & USB_MUX_FLAG_IN_LPM)
		return USB_PD_MUX_NONE;

	rv = configure_mux(port, USB_MUX_GET_MODE, &mux_state);

	return rv ? USB_PD_MUX_NONE : mux_state;
}

void usb_mux_flip(int port)
{
	mux_state_t mux_state;

	if (port >= board_get_usb_pd_port_count()) {
		return;
	}

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

	if (exit_low_power_mode(port) != EC_SUCCESS)
		return;

	if (configure_mux(port, USB_MUX_GET_MODE, &mux_state))
		return;

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		mux_state &= ~USB_PD_MUX_POLARITY_INVERTED;
	else
		mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	configure_mux(port, USB_MUX_SET_MODE, &mux_state);
}

static void perform_mux_hpd_update(int port, mux_state_t hpd_state)
{
	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

	if (exit_low_power_mode(port) != EC_SUCCESS)
		return;

	configure_mux(port, USB_MUX_HPD_UPDATE, &hpd_state);
}

void usb_mux_hpd_update(int port, mux_state_t hpd_state)
{
	if (port >= board_get_usb_pd_port_count())
		return;

	/* Send to the mux task if present to maintain sequencing with sets */
	if (IS_ENABLED(HAS_TASK_USB_MUX))
		mux_task_enqueue(port, USB_MUX_HPD_UPDATE, hpd_state,
				 0, 0);
	else
		perform_mux_hpd_update(port, hpd_state);
}

int usb_mux_retimer_fw_update_port_info(void)
{
	int i;
	int port_info = 0;
	const struct usb_mux *mux_ptr;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		mux_ptr = &usb_muxes[i];
		while (mux_ptr) {
			if (mux_ptr->driver &&
				mux_ptr->driver->is_retimer_fw_update_capable &&
				mux_ptr->driver->is_retimer_fw_update_capable())
				port_info |= BIT(i);
			mux_ptr = mux_ptr->next_mux;
		}
	}
	return port_info;
}

static void mux_chipset_reset(void)
{
	int port;

	for (port = 0; port < board_get_usb_pd_port_count(); ++port)
		configure_mux(port, USB_MUX_CHIPSET_RESET, NULL);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, mux_chipset_reset, HOOK_PRIO_DEFAULT);

/*
 * For muxes which have powered off in G3, clear any cached INIT and LPM flags
 * since the chip will need reset.
 */
static void usb_mux_reset_in_g3(void)
{
	int port;
	const struct usb_mux *mux_ptr;

	for (port = 0; port < board_get_usb_pd_port_count(); port++) {
		mux_ptr = &usb_muxes[port];

		while (mux_ptr) {
			if (mux_ptr->flags & USB_MUX_FLAG_RESETS_IN_G3) {
				atomic_clear_bits(&flags[port],
						  USB_MUX_FLAG_INIT |
						  USB_MUX_FLAG_IN_LPM);
			}
			mux_ptr = mux_ptr->next_mux;
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, usb_mux_reset_in_g3, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_TYPEC
static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb", "dp", "dock"};
	char *e;
	int port;
	mux_state_t mux = USB_PD_MUX_NONE;
	int i;

	if (argc == 2 && !strcasecmp(argv[1], "debug")) {
		enable_debug_prints = 1;
		return EC_SUCCESS;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		mux_state_t mux_state;

		mux_state = usb_mux_get(port);
		ccprintf("Port %d: USB=%d DP=%d POLARITY=%s HPD_IRQ=%d "
			"HPD_LVL=%d SAFE=%d TBT=%d USB4=%d\n", port,
			!!(mux_state & USB_PD_MUX_USB_ENABLED),
			!!(mux_state & USB_PD_MUX_DP_ENABLED),
			mux_state & USB_PD_MUX_POLARITY_INVERTED ?
				"INVERTED" : "NORMAL",
			!!(mux_state & USB_PD_MUX_HPD_IRQ),
			!!(mux_state & USB_PD_MUX_HPD_LVL),
			!!(mux_state & USB_PD_MUX_SAFE_MODE),
			!!(mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED),
			!!(mux_state & USB_PD_MUX_USB4_ENABLED));

		return EC_SUCCESS;
	}

	for (i = 0; i < ARRAY_SIZE(mux_name); i++)
		if (!strcasecmp(argv[2], mux_name[i]))
			mux = i;
	usb_mux_set(port, mux, mux == USB_PD_MUX_NONE ?
				      USB_SWITCH_DISCONNECT :
				      USB_SWITCH_CONNECT,
			  polarity_rm_dts(pd_get_polarity(port)));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(typec, command_typec,
			"[port|debug] [none|usb|dp|dock]",
			"Control type-C connector muxing");
#endif

static enum ec_status hc_usb_pd_mux_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_mux_info *p = args->params;
	struct ec_response_usb_pd_mux_info *r = args->response;
	int port = p->port;
	mux_state_t mux_state;

	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (configure_mux(port, USB_MUX_GET_MODE, &mux_state))
		return EC_RES_ERROR;

	r->flags = mux_state;

	/* Clear HPD IRQ event since we're about to inform host of it. */
	if (IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) &&
	    (r->flags & USB_PD_MUX_HPD_IRQ)) {
		usb_mux_hpd_update(port, r->flags & USB_PD_MUX_HPD_LVL);
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_MUX_INFO,
		     hc_usb_pd_mux_info,
		     EC_VER_MASK(0));

static enum ec_status hc_usb_pd_mux_ack(struct host_cmd_handler_args *args)
{
	__maybe_unused const struct ec_params_usb_pd_mux_ack *p = args->params;

	if (!IS_ENABLED(CONFIG_USB_MUX_AP_ACK_REQUEST))
		return EC_RES_INVALID_COMMAND;

	if (ack_task[p->port] != TASK_ID_INVALID)
		task_set_event(ack_task[p->port], PD_EVENT_AP_MUX_DONE);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_MUX_ACK,
		     hc_usb_pd_mux_ack,
		     EC_VER_MASK(0));
