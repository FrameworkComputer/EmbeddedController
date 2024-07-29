/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Event handling in MKBP keyboard protocol
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "atomic.h"
#include "chipset.h"
#include "gpio.h"
#include "host_command.h"
#include "host_command_heci.h"
#include "hwtimer.h"
#include "keyboard_config.h"
#include "link_defs.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "power.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/*
 * Tracks the current state of the MKBP interrupt send from the EC to the AP.
 *
 * The inactive state is only valid when there are no events to set to the AP.
 * If the AP is asleep, then some events are not worth waking the AP up, so the
 * interrupt could remain in an inactive in that case.
 *
 * The transition state (INTERRUPT_INACTIVE_TO_ACTIVE) is used to track the
 * sometimes lock transition for a "rising edge" for platforms that send the
 * rising edge interrupt through a host communication layer
 *
 * The active state represents that a rising edge interrupt has already been
 * sent to the AP, and the EC is waiting for the AP to call get next event
 * host command to consume all of the events (at which point the state will
 * move to inactive).
 *
 * The transition from ACTIVE -> INACTIVE is considerer to be simple meaning
 * the operation can be performed within a blocking mutex (e.g. no-op or setting
 * a gpio).
 */
enum interrupt_state {
	INTERRUPT_INACTIVE,
	INTERRUPT_INACTIVE_TO_ACTIVE, /* Transitioning */
	INTERRUPT_ACTIVE,
};

struct mkbp_state {
	mutex_t lock;
	uint32_t events;
	enum interrupt_state interrupt;
	/*
	 * Tracks unique transitions to INTERRUPT_INACTIVE_TO_ACTIVE allowing
	 * only the most recent transition to finish the transition to a final
	 * state -- either active or inactive depending on the result of the
	 * operation.
	 */
	uint8_t interrupt_id;
	/*
	 * Tracks the number of consecutive failed attempts for the AP to poll
	 * get_next_events in order to limit the retry logic.
	 */
	uint8_t failed_attempts;
};

static struct mkbp_state state;
uint32_t mkbp_last_event_time;

static const int ap_comm_failure_threshold = 2;
static int ap_comm_failure_count;

#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
static uint32_t mkbp_event_wake_mask = CONFIG_MKBP_EVENT_WAKEUP_MASK;
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */

#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
static uint32_t mkbp_host_event_wake_mask = CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK;
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */

#ifdef CONFIG_ZEPHYR
static int init_mkbp_mutex(void)
{
	k_mutex_init(&state.lock);

	return 0;
}
SYS_INIT(init_mkbp_mutex, POST_KERNEL, 50);
#endif /* CONFIG_ZEPHYR */

#if defined(CONFIG_MKBP_USE_GPIO) || \
	defined(CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT)
static int mkbp_set_host_active_via_gpio(int active, uint32_t *timestamp)
{
	uint32_t lock_key;
	/*
	 * If we want to take a timestamp, then disable interrupts temporarily
	 * to ensure that the timestamp is as close as possible to the setting
	 * of the GPIO pin in hardware (i.e. we aren't interrupted between
	 * taking the timestamp and setting the gpio)
	 */
	if (timestamp) {
		lock_key = irq_lock();
		*timestamp = __hw_clock_source_read();
	}

	if (IS_ENABLED(CONFIG_MKBP_USE_GPIO_ACTIVE_HIGH))
		gpio_set_level(GPIO_EC_INT_L, active);
	else
		gpio_set_level(GPIO_EC_INT_L, !active);

	if (timestamp)
		irq_unlock(lock_key);

#ifdef CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT
	/*
	 * In case EC_INT_L is not a wake pin, make sure that we also attempt to
	 * wake the AP via a host event.  Only use this second notification
	 * interface in suspend since MKBP events are a part of the
	 * HOST_EVENT_ALWAYS_REPORT_DEFAULT_MASK. This can cause an MKBP host
	 * event to be set in S0, but not triggering an SCI since the event is
	 * not in the SCI mask.  This would also cause the board to prematurely
	 * wake up when suspending due to the lingering event.
	 */
	if (active && chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		host_set_single_event(EC_HOST_EVENT_MKBP);
#endif /* CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT */

	return EC_SUCCESS;
}
#endif /* CONFIG_MKBP_USE_GPIO(_AND_HOST_EVENT)? */

#ifdef CONFIG_MKBP_USE_HOST_EVENT
static int mkbp_set_host_active_via_event(int active, uint32_t *timestamp)
{
	/* This should be moved into host_set_single_event for more accuracy */
	if (timestamp)
		*timestamp = __hw_clock_source_read();
	if (active)
		host_set_single_event(EC_HOST_EVENT_MKBP);
	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_MKBP_USE_HECI
static int mkbp_set_host_active_via_heci(int active, uint32_t *timestamp)
{
	if (active)
		return heci_send_mkbp_event(timestamp);
	return EC_SUCCESS;
}
#endif

/*
 * This communicates to the AP whether an MKBP event is currently available
 * for processing.
 *
 * NOTE: When active is 0 this function CANNOT de-schedule. It must be very
 * simple like toggling a GPIO or no-op
 *
 * @param active  1 if there is an event, 0 otherwise
 * @param timestamp, if non-null this variable will be written as close to the
 *			hardware interrupt from EC->AP as possible.
 */
static int mkbp_set_host_active(int active, uint32_t *timestamp)
{
#if defined(CONFIG_MKBP_USE_CUSTOM)
	return mkbp_set_host_active_via_custom(active, timestamp);
#elif defined(CONFIG_MKBP_USE_HOST_EVENT)
	return mkbp_set_host_active_via_event(active, timestamp);
#elif defined(CONFIG_MKBP_USE_GPIO) || \
	defined(CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT)
	return mkbp_set_host_active_via_gpio(active, timestamp);
#elif defined(CONFIG_MKBP_USE_HECI)
	return mkbp_set_host_active_via_heci(active, timestamp);
#endif
}

#if defined(CONFIG_MKBP_EVENT_WAKEUP_MASK) || \
	defined(CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK)
/**
 * Check if the host is sleeping. Check our power state in addition to the
 * self-reported sleep state of host (CONFIG_POWER_TRACK_HOST_SLEEP_STATE).
 */
static inline int host_is_sleeping(void)
{
	int is_sleeping = !chipset_in_state(CHIPSET_STATE_ON);

#ifdef CONFIG_POWER_TRACK_HOST_SLEEP_STATE
	enum host_sleep_event sleep_state = power_get_host_sleep_state();
	is_sleeping |= (sleep_state == HOST_SLEEP_EVENT_S0IX_SUSPEND ||
			sleep_state == HOST_SLEEP_EVENT_S3_SUSPEND ||
			sleep_state == HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND);
#endif
	return is_sleeping;
}
#endif /* CONFIG_MKBP_(HOST_EVENT_)?WAKEUP_MASK */

/*
 * This is the deferred function that ensures that we attempt to set the MKBP
 * interrupt again if there was a failure in the system (EC or AP) and the AP
 * never called mkbp_fifo_get_next_event.
 */
static void force_mkbp_if_events(void);
DECLARE_DEFERRED(force_mkbp_if_events);

test_export_static void activate_mkbp_with_events(uint32_t events_to_add)
{
	int interrupt_id = -1;
	int skip_interrupt = 0;
	int rv, schedule_deferred = 0;

#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
	/*
	 * Check to see if this host event should wake the system.
	 * Use == instead of & here since we don't want to apply the host event
	 * skipping logic if we are adding a host event and something else.
	 */
	if (events_to_add == BIT(EC_MKBP_EVENT_HOST_EVENT) ||
	    events_to_add == BIT(EC_MKBP_EVENT_HOST_EVENT64))
		skip_interrupt =
			host_is_sleeping() &&
			!(host_get_events() & mkbp_host_event_wake_mask);
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */

#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
	/* Check to see if this MKBP event should wake the system. */
	if (!skip_interrupt)
		skip_interrupt = host_is_sleeping() &&
				 !(events_to_add & mkbp_event_wake_mask);
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */

	mutex_lock(&state.lock);
	state.events |= events_to_add;

	/* To skip the interrupt, we cannot have the EC_MKBP_EVENT_KEY_MATRIX */
	skip_interrupt = skip_interrupt &&
			 !(state.events & BIT(EC_MKBP_EVENT_KEY_MATRIX));

	if (state.events && state.interrupt == INTERRUPT_INACTIVE &&
	    !skip_interrupt) {
		state.interrupt = INTERRUPT_INACTIVE_TO_ACTIVE;
		interrupt_id = ++state.interrupt_id;
	}
	mutex_unlock(&state.lock);

	/* If we don't need to send an interrupt we are done */
	if (interrupt_id < 0)
		return;

	/* Send a rising edge MKBP interrupt */
	rv = mkbp_set_host_active(1, &mkbp_last_event_time);

	/*
	 * If this was the last interrupt to the AP, update state;
	 * otherwise the latest interrupt should update state.
	 */
	mutex_lock(&state.lock);
	if (state.interrupt == INTERRUPT_INACTIVE_TO_ACTIVE &&
	    interrupt_id == state.interrupt_id) {
		schedule_deferred = 1;
		state.interrupt = rv == EC_SUCCESS ? INTERRUPT_ACTIVE :
						     INTERRUPT_INACTIVE;
	}
	mutex_unlock(&state.lock);

	if (schedule_deferred) {
		hook_call_deferred(&force_mkbp_if_events_data, SECOND);
		if (rv != EC_SUCCESS)
			CPRINTS("Could not activate MKBP (%d). Deferring", rv);
	}
}

/*
 * This is the deferred function that ensures that we attempt to set the MKBP
 * interrupt again if there was a failure in the system (EC or AP) and the AP
 * never called mkbp_fifo_get_next_event.
 */
static void force_mkbp_if_events(void)
{
	int toggled = 0;
	int send_mkbp_interrupt = 0;

	mutex_lock(&state.lock);
	if (state.interrupt == INTERRUPT_INACTIVE) {
		/*
		 * When this function is called with state of interrupt set
		 * to INACTIVE, it means that EC failed to send MKBP interrupt
		 * to AP. In this case we are going to send interrupt once
		 * again (without limits).
		 */
		send_mkbp_interrupt = 1;
	} else if (state.interrupt == INTERRUPT_ACTIVE) {
		/*
		 * When this function is called with state of interrupt set
		 * to ACTIVE, it means that AP failed to respond.
		 *
		 * It is safe to mark interrupt state as INACTIVE, because
		 * force_mkbp_with_events() function can be only scheduled by
		 * activate_mkbp_with_event() which will set interrupt state
		 * to ACTIVE (and allow to increment failed_attempts counter).
		 * After 3 attempts, we are setting interrupt state to INACTIVE
		 * but we are not going to call activate_mkbp_with_events().
		 * This was meant to unblock MKBP interrupt mechanism for new
		 * events.
		 */
		state.interrupt = INTERRUPT_INACTIVE;
		/*
		 * Failed attempts counter is cleared only when AP pulls all
		 * of events or we exceed number of attempts, so marking
		 * interrupt as INACTIVE doesn't affect failed_attempts counter.
		 * If we need to send interrupt once again
		 * activate_mkbp_with_events() will set interrupt state to
		 * ACTIVE before this function will be called.
		 */
		++ap_comm_failure_count;
		if (++state.failed_attempts < 3) {
			send_mkbp_interrupt = 1;
			toggled = 1;
		} else {
			/*
			 * If we exceed maximum number of failed attempts we
			 * will stop trying to send MKBP interrupt for current
			 * event (send_mkbp_interrupt == 0), but leaving
			 * possibility to send MKBP interrupts for future
			 * events (state of interrupt makred as inactive).
			 * Future events should have a chance to be sent
			 * 3 times, so we should clear failed attempts
			 * counter now
			 */
			state.failed_attempts = 0;
		}
	}
	mutex_unlock(&state.lock);

	if (toggled) {
		/**
		 * Don't spam the EC logs when the AP is hung. Instead, log the
		 * first few failures, and then indicate the AP is likely hung.
		 */
		if (ap_comm_failure_count < ap_comm_failure_threshold) {
			CPRINTS("MKBP not cleared within threshold, toggling.");
		} else if (ap_comm_failure_count == ap_comm_failure_threshold) {
			if (chipset_in_state(CHIPSET_STATE_ON))
				CPRINTS("MKBP: The AP is failing to respond "
					"despite being powered on.");
			else
				CPRINTS("MKBP: The AP is failing to respond "
					"because it is sleeping or off");
		}
	}

	if (send_mkbp_interrupt)
		activate_mkbp_with_events(0);
}

test_mockable int mkbp_send_event(uint8_t event_type)
{
	activate_mkbp_with_events(BIT(event_type));

	return 1;
}

static int set_inactive_if_no_events(void)
{
	int interrupt_cleared;

	mutex_lock(&state.lock);
	interrupt_cleared = !state.events;
	if (interrupt_cleared) {
		state.interrupt = INTERRUPT_INACTIVE;
		state.failed_attempts = 0;
		/* Only simple tasks (i.e. gpio set or no-op) allowed here */
		mkbp_set_host_active(0, NULL);
	}
	mutex_unlock(&state.lock);

	/* Cancel our safety net since the events were cleared. */
	if (interrupt_cleared) {
		hook_call_deferred(&force_mkbp_if_events_data, -1);
		/**
		 * This AP communication was successful.
		 * Reset the count to log the next AP communication failure.
		 */
		ap_comm_failure_count = 0;
	}

	return interrupt_cleared;
}

/* This can only be called when the state.lock mutex is held */
static int take_event_if_set(uint8_t event_type)
{
	int taken;

	taken = state.events & BIT(event_type);
	state.events &= ~BIT(event_type);

	return taken;
}

static const struct mkbp_event_source *find_mkbp_event_source(uint8_t type)
{
#ifdef CONFIG_ZEPHYR
	return zephyr_find_mkbp_event_source(type);
#else
	const struct mkbp_event_source *src;

	for (src = __mkbp_evt_srcs; src < __mkbp_evt_srcs_end; ++src)
		if (src->event_type == type)
			break;

	if (src == __mkbp_evt_srcs_end)
		return NULL;

	return src;
#endif
}

static enum ec_status mkbp_get_next_event(struct host_cmd_handler_args *args)
{
	static int last;
	int i, evt;
	struct ec_response_get_next_event_v3 *r = args->response;
	const struct mkbp_event_source *src;

	int data_size = -EC_ERROR_BUSY;

	memset(args->response, 0, args->response_max);
	do {
		/*
		 * Find the next event to service.  We do this in a round-robin
		 * way to make sure no event gets starved.
		 */
		mutex_lock(&state.lock);
		for (i = 0; i < EC_MKBP_EVENT_COUNT; ++i)
			if (take_event_if_set((last + i) % EC_MKBP_EVENT_COUNT))
				break;
		mutex_unlock(&state.lock);

		if (i == EC_MKBP_EVENT_COUNT) {
			if (set_inactive_if_no_events())
				return EC_RES_UNAVAILABLE;
			/* An event was set just now, restart loop. */
			continue;
		}

		evt = (i + last) % EC_MKBP_EVENT_COUNT;
		last = evt + 1;

		src = find_mkbp_event_source(evt);
		if (src == NULL)
			return EC_RES_ERROR;

		r->event_type = evt;

		/*
		 * get_data() can return -EC_ERROR_BUSY which indicates that the
		 * next element in the keyboard FIFO does not match what we were
		 * called with.  For example, get_data is expecting a keyboard
		 * matrix, however the next element in the FIFO is a button
		 * event instead.  Therefore, we have to service that button
		 * event first.
		 */
		data_size = src->get_data((uint8_t *)&r->data);
		if (data_size == -EC_ERROR_BUSY) {
			mutex_lock(&state.lock);
			state.events |= BIT(evt);
			mutex_unlock(&state.lock);
		}
	} while (data_size == -EC_ERROR_BUSY);

	/*
	 * Drop last columns if we send a key matrix with numpad to a v0 or v1
	 * request.
	 */
	if (r->event_type == EC_MKBP_EVENT_KEY_MATRIX) {
		size_t max_size;
		switch (args->version) {
		case 0:
			max_size = member_size(union ec_response_get_next_data,
					       key_matrix);

			break;
		case 1:
		case 2:
			max_size = member_size(
				union ec_response_get_next_data_v1, key_matrix);
			break;
		default:
			max_size = member_size(
				union ec_response_get_next_data_v3, key_matrix);
		}
		data_size = MIN(data_size, max_size);
	}

	/* If there are no more events and we support the "more" flag, set it */
	if (!set_inactive_if_no_events() && args->version >= 2)
		r->event_type |= EC_MKBP_HAS_MORE_EVENTS;

	if (data_size < 0)
		return EC_RES_ERROR;
	args->response_size = 1 + data_size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_NEXT_EVENT, mkbp_get_next_event,
		     EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2) |
			     EC_VER_MASK(3));

#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
#ifndef CONFIG_HOSTCMD_X86
static enum ec_status
mkbp_get_host_event_wake_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r = args->response;

	r->mask = mkbp_host_event_wake_mask;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_WAKE_MASK,
		     mkbp_get_host_event_wake_mask, EC_VER_MASK(0));
#endif /* !CONFIG_HOSTCMD_X86 */
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */

#if defined(CONFIG_MKBP_EVENT_WAKEUP_MASK) || \
	defined(CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK)
static enum ec_status hc_mkbp_wake_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_mkbp_event_wake_mask *r = args->response;
	const struct ec_params_mkbp_event_wake_mask *p = args->params;
	enum ec_mkbp_event_mask_action action = p->action;

	switch (action) {
	case GET_WAKE_MASK:
		switch (p->mask_type) {
#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
		case EC_MKBP_HOST_EVENT_WAKE_MASK:
			r->wake_mask = mkbp_host_event_wake_mask;
			break;
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */

#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
		case EC_MKBP_EVENT_WAKE_MASK:
			r->wake_mask = mkbp_event_wake_mask;
			break;
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */

		default:
			/* Unknown mask, or mask is not in use. */
			return EC_RES_INVALID_PARAM;
		}

		args->response_size = sizeof(*r);
		break;

	case SET_WAKE_MASK:
		args->response_size = 0;

		switch (p->mask_type) {
#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
		case EC_MKBP_HOST_EVENT_WAKE_MASK:
			CPRINTF("MKBP hostevent mask updated to: 0x%08x "
				"(was 0x%08x)\n",
				p->new_wake_mask, mkbp_host_event_wake_mask);
			mkbp_host_event_wake_mask = p->new_wake_mask;
			break;
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */

#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
		case EC_MKBP_EVENT_WAKE_MASK:
			mkbp_event_wake_mask = p->new_wake_mask;
			CPRINTF("MKBP event mask updated to: 0x%08x\n",
				mkbp_event_wake_mask);
			break;
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */

		default:
			/* Unknown mask, or mask is not in use. */
			return EC_RES_INVALID_PARAM;
		}
		break;

	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_WAKE_MASK, hc_mkbp_wake_mask, EC_VER_MASK(0));

static int command_mkbp_wake_mask(int argc, const char **argv)
{
	if (argc == 3) {
		char *e;
		uint32_t new_mask = strtoull(argv[2], &e, 0);

		if (*e)
			return EC_ERROR_PARAM2;

#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
		if (strncmp(argv[1], "event", 5) == 0)
			mkbp_event_wake_mask = new_mask;
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */

#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
		if (strncmp(argv[1], "hostevent", 9) == 0)
			mkbp_host_event_wake_mask = new_mask;
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */
	} else if (argc != 1) {
		return EC_ERROR_PARAM_COUNT;
	}

#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
	ccprintf("MKBP host event wake mask: 0x%08x\n",
		 mkbp_host_event_wake_mask);
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */
#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
	ccprintf("MKBP event wake mask: 0x%08x\n", mkbp_event_wake_mask);
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mkbpwakemask, command_mkbp_wake_mask,
			"[event | hostevent] [new_mask]",
			"Show or set MKBP event/hostevent wake mask");
#endif /* CONFIG_MKBP_(HOST)?EVENT_WAKEUP_MASK */

#ifdef TEST_BUILD
void mkbp_event_clear_all(void)
{
	mutex_lock(&state.lock);
	state.events = 0;
	mutex_unlock(&state.lock);

	/* Reset the interrupt line */
	mkbp_set_host_active(0, NULL);
#ifdef CONFIG_MKBP_EVENT_WAKEUP_MASK
	mkbp_event_wake_mask = CONFIG_MKBP_EVENT_WAKEUP_MASK;
#endif /* CONFIG_MKBP_EVENT_WAKEUP_MASK */

#ifdef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
	mkbp_host_event_wake_mask = CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK;
#endif /* CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK */
}
#endif

#ifdef CONFIG_EMULATED_SYSRQ
void host_send_sysrq(uint8_t key)
{
	uint32_t value = key;

	mkbp_fifo_add(EC_MKBP_EVENT_SYSRQ, (const uint8_t *)&value);
}
#endif
