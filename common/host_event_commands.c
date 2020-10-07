/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host event commands for Chrome EC */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lpc.h"
#include "mkbp_event.h"
#include "power.h"
#include "system.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_EVENTS, outstr)
#define CPRINTS(format, args...) cprints(CC_EVENTS, format, ## args)

/*
 * This is used to avoid 64-bit shifts which might require a new library
 * function.
 */
#define HOST_EVENT_32BIT_MASK(x)	(1UL << ((x) - 1))
static void host_event_set_bit(host_event_t *ev, uint8_t bit)
{
	uint32_t *ptr = (uint32_t *)ev;

	*ev = 0;

	/*
	 * Host events are 1-based, so return early if event 0 is requested to
	 * be set.
	 */
	if (bit == 0)
		return;

#ifdef CONFIG_HOST_EVENT64
	if (bit > 32)
		*(ptr + 1) = HOST_EVENT_32BIT_MASK(bit - 32);
	else
#endif
		*ptr = HOST_EVENT_32BIT_MASK(bit);
}

#ifdef CONFIG_HOSTCMD_X86

#define LPC_SYSJUMP_TAG 0x4c50  /* "LP" */
#define LPC_SYSJUMP_OLD_VERSION 1
#define LPC_SYSJUMP_VERSION 2

/*
 * Always report mask includes mask of host events that need to be reported in
 * host event always irrespective of the state of SCI, SMI and wake masks.
 *
 * Events that indicate critical shutdown/reboots that have occurred:
 *   - EC_HOST_EVENT_THERMAL_SHUTDOWN
 *   - EC_HOST_EVENT_BATTERY_SHUTDOWN
 *   - EC_HOST_EVENT_HANG_REBOOT
 *   - EC_HOST_EVENT_PANIC
 *
 * Events that are consumed by BIOS:
 *   - EC_HOST_EVENT_KEYBOARD_RECOVERY
 *   - EC_HOST_EVENT_KEYBOARD_FASTBOOT
 *   - EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT
 *
 * Events that are buffered and have separate data maintained of their own:
 *   - EC_HOST_EVENT_MKBP
 *
 */
#define LPC_HOST_EVENT_ALWAYS_REPORT_DEFAULT_MASK			\
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_THERMAL_SHUTDOWN) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_BATTERY_SHUTDOWN) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_HANG_REBOOT) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_PANIC) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_FASTBOOT) |		\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MKBP) |			\
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT))

static host_event_t lpc_host_events;
static host_event_t lpc_host_event_mask[LPC_HOST_EVENT_COUNT];

/* Indicates if active wake mask set by host */
static uint8_t active_wm_set_by_host;

void lpc_set_host_event_mask(enum lpc_host_event_type type, host_event_t mask)
{
	lpc_host_event_mask[type] = mask;
	lpc_update_host_event_status();

	/* mask 0 indicates wake mask not set by host */
	if ((type == LPC_HOST_EVENT_WAKE) && (mask == 0))
		active_wm_set_by_host = 0;
}

host_event_t lpc_get_host_event_mask(enum lpc_host_event_type type)
{
	return lpc_host_event_mask[type];
}

static host_event_t lpc_get_all_host_event_masks(void)
{
	host_event_t or_mask = 0;
	int i;

	for (i = 0; i < LPC_HOST_EVENT_COUNT; i++)
		or_mask |= lpc_get_host_event_mask(i);

	return or_mask;
}

static void lpc_set_host_event_state(host_event_t events)
{
	if (events == lpc_host_events)
		return;

	lpc_host_events = events;
	lpc_update_host_event_status();
}

host_event_t lpc_get_host_events_by_type(enum lpc_host_event_type type)
{
	return lpc_host_events & lpc_get_host_event_mask(type);
}

host_event_t lpc_get_host_events(void)
{
	return lpc_host_events;
}

int lpc_get_next_host_event(void)
{
	host_event_t ev;
	int evt_idx =  __builtin_ffs(lpc_host_events);

#ifdef CONFIG_HOST_EVENT64
	if (evt_idx == 0) {
		int evt_idx_high = __builtin_ffs(lpc_host_events >> 32);

		if (evt_idx_high)
			evt_idx = 32 + evt_idx_high;
	}
#endif

	if (evt_idx) {
		host_event_set_bit(&ev, evt_idx);
		host_clear_events(ev);
	}
	return evt_idx;
}

static void lpc_sysjump_save_mask(void)
{
	system_add_jump_tag(LPC_SYSJUMP_TAG, LPC_SYSJUMP_VERSION,
			    sizeof(lpc_host_event_mask), lpc_host_event_mask);
}
DECLARE_HOOK(HOOK_SYSJUMP, lpc_sysjump_save_mask, HOOK_PRIO_DEFAULT);

/*
 * Restore various LPC masks if they were saved before the sysjump.
 *
 * Returns:
 * 1 = All masks were restored
 * 0 = No masks were stashed before sysjump or EC performing sysjump did not
 *     support always report mask.
 */
static int lpc_post_sysjump_restore_mask(void)
{
	const host_event_t *prev_mask;
	int size, version;

	prev_mask = (const host_event_t *)system_get_jump_tag(LPC_SYSJUMP_TAG,
			&version, &size);
	if (!prev_mask || size != sizeof(lpc_host_event_mask) ||
	    (version != LPC_SYSJUMP_VERSION &&
	     version != LPC_SYSJUMP_OLD_VERSION))
		return 0;

	memcpy(lpc_host_event_mask, prev_mask, sizeof(lpc_host_event_mask));

	return version == LPC_SYSJUMP_VERSION;
}

host_event_t __attribute__((weak)) lpc_override_always_report_mask(void)
{
	return LPC_HOST_EVENT_ALWAYS_REPORT_DEFAULT_MASK;
}

void lpc_init_mask(void)
{
	/*
	 * First check if masks were stashed before sysjump. If no masks were
	 * stashed or if the EC image performing sysjump does not support always
	 * report mask, then set always report mask now.
	 */
	if (!lpc_post_sysjump_restore_mask())
		lpc_host_event_mask[LPC_HOST_EVENT_ALWAYS_REPORT] =
			lpc_override_always_report_mask();
}

void lpc_s3_resume_clear_masks(void)
{
	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, 0);
}

/*
 * Clear events that are not part of SCI/SMI mask so as to prevent
 * premature wakes on next suspend(S0ix). This is not needed on
 * suspending to S3 as coreboot clears all events on path to suspend.
 *
 * We preserve events that are part of SCI/SMI mask to help kernel
 * identify the wake reason on resume. For events that are not set
 * in SCI mask but are part of S0iX WAKE masks, kernel drivers should
 * have other ways (physical/virtual interrupt) pin to identify when
 * they trigger wakes.
 */
#ifdef CONFIG_POWER_S0IX
void clear_non_sci_events(void)
{
	host_clear_events(~lpc_get_host_event_mask(LPC_HOST_EVENT_SCI) &
			  ~lpc_get_host_event_mask(LPC_HOST_EVENT_SMI));
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, clear_non_sci_events, HOOK_PRIO_DEFAULT);
#endif

#endif

/*
 * Maintain two copies of the events that are set.
 *
 * The primary copy is mirrored in mapped memory and used to trigger interrupts
 * on the host via ACPI/SCI/SMI/GPIO.
 *
 * The secondary (B) copy is used by entities other than ACPI to query the state
 * of host events on EC. Currently events_copy_b is used for
 *      1. Logging recovery mode switch in coreboot.
 *      2. Used by depthcharge on devices with no 8042 and no MKBP interrupt.
 *      3. Logging wake reason in coreboot.
 * Current query of a event from copy_b is immediately followed by clear of the
 * same event. Further uses of copy_b should make sure this semantics is
 * followed and none of the above mentioned use cases are broken.
 *
 * Setting an event sets both copies.  Copies are cleared separately.
 */
static host_event_t events;
static host_event_t events_copy_b;

/* Lazy wake masks */
#ifdef CONFIG_HOSTCMD_X86
static struct lazy_wake_masks {
	host_event_t s3_lazy_wm;
	host_event_t s5_lazy_wm;
#ifdef CONFIG_POWER_S0IX
	host_event_t s0ix_lazy_wm;
#endif
} lazy_wm;
#endif

static void host_events_atomic_or(host_event_t *e, host_event_t m)
{
	uint32_t *ptr = (uint32_t *)e;

	deprecated_atomic_or(ptr, (uint32_t)m);
#ifdef CONFIG_HOST_EVENT64
	deprecated_atomic_or(ptr + 1, (uint32_t)(m >> 32));
#endif
}

static void host_events_atomic_clear(host_event_t *e, host_event_t m)
{
	uint32_t *ptr = (uint32_t *)e;

	deprecated_atomic_clear_bits(ptr, (uint32_t)m);
#ifdef CONFIG_HOST_EVENT64
	deprecated_atomic_clear_bits(ptr + 1, (uint32_t)(m >> 32));
#endif
}

#if !defined(CONFIG_HOSTCMD_X86) && defined(CONFIG_MKBP_EVENT)
static void host_events_send_mkbp_event(host_event_t e)
{
#ifdef CONFIG_HOST_EVENT64
	/*
	 * If event bits in the upper 32-bit are set, indicate 64-bit host
	 * event.
	 */
	if (!(uint32_t)e)
		mkbp_send_event(EC_MKBP_EVENT_HOST_EVENT64);
	else
#endif
		mkbp_send_event(EC_MKBP_EVENT_HOST_EVENT);
}
#endif

host_event_t host_get_events(void)
{
	return events;
}

void host_set_events(host_event_t mask)
{
	/* ignore host events the rest of board doesn't care about */
#ifdef CONFIG_HOST_EVENT64
	mask &= CONFIG_HOST_EVENT64_REPORT_MASK;
#else
	mask &= CONFIG_HOST_EVENT_REPORT_MASK;
#endif

#ifdef CONFIG_HOSTCMD_X86
	/*
	 * Host only cares about the events for which the masks are set either
	 * in wake mask, SCI mask or SMI mask. In addition to that, there are
	 * certain events that need to be always reported (Please see
	 * LPC_HOST_EVENT_ALWAYS_REPORT_DEFAULT_MASK). Thus, when a new host
	 * event is being set, ensure that it is present in one of these
	 * masks. Else, there is no need to process that event.
	 */
	mask &= lpc_get_all_host_event_masks();
#endif

	/* exit now if nothing has changed */
	if (!((events & mask) != mask || (events_copy_b & mask) != mask))
		return;

	HOST_EVENT_CPRINTS("event set", mask);

	host_events_atomic_or(&events, mask);
	host_events_atomic_or(&events_copy_b, mask);

#ifdef CONFIG_HOSTCMD_X86
	lpc_set_host_event_state(events);
#else
	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = events;
#ifdef CONFIG_MKBP_EVENT
#ifdef CONFIG_MKBP_USE_HOST_EVENT
#error "Config error: MKBP must not be on top of host event"
#endif
	host_events_send_mkbp_event(events);
#endif  /* CONFIG_MKBP_EVENT */
#endif  /* !CONFIG_HOSTCMD_X86 */
}

void host_set_single_event(enum host_event_code event)
{
	host_event_t ev = 0;

	host_event_set_bit(&ev, event);
	host_set_events(ev);
}

int host_is_event_set(enum host_event_code event)
{
	host_event_t ev = 0;

	host_event_set_bit(&ev, event);
	return events & ev;
}

void host_clear_events(host_event_t mask)
{
	/* ignore host events the rest of board doesn't care about */
#ifdef CONFIG_HOST_EVENT64
	mask &= CONFIG_HOST_EVENT64_REPORT_MASK;
#else
	mask &= CONFIG_HOST_EVENT_REPORT_MASK;
#endif

	/* return early if nothing changed */
	if (!(events & mask))
		return;

	HOST_EVENT_CPRINTS("event clear", mask);

	host_events_atomic_clear(&events, mask);

#ifdef CONFIG_HOSTCMD_X86
	lpc_set_host_event_state(events);
#else
	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = events;
#ifdef CONFIG_MKBP_EVENT
	host_events_send_mkbp_event(events);
#endif
#endif  /* !CONFIG_HOSTCMD_X86 */
}

#ifndef CONFIG_HOSTCMD_X86
static int host_get_next_event(uint8_t *out)
{
	uint32_t event_out = (uint32_t)events;
	memcpy(out, &event_out, sizeof(event_out));
	host_events_atomic_clear(&events, event_out);
	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = events;
	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_HOST_EVENT, host_get_next_event);

#ifdef CONFIG_HOST_EVENT64
static int host_get_next_event64(uint8_t *out)
{
	host_event_t event_out = events;

	memcpy(out, &event_out, sizeof(event_out));
	host_events_atomic_clear(&events, event_out);
	*(host_event_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS) = events;
	return sizeof(event_out);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_HOST_EVENT64, host_get_next_event64);
#endif
#endif

/**
 * Clear one or more host event bits from copy B.
 *
 * @param mask          Event bits to clear (use EC_HOST_EVENT_MASK()).
 *                      Write 1 to a bit to clear it.
 */
static void host_clear_events_b(host_event_t mask)
{
	/* Only print if something's about to change */
	if (events_copy_b & mask)
		HOST_EVENT_CPRINTS("event clear B", mask);

	host_events_atomic_clear(&events_copy_b, mask);
}

/**
 * Politely ask the CPU to enable/disable its own throttling.
 *
 * @param throttle	Enable (!=0) or disable(0) throttling
 */
test_mockable void host_throttle_cpu(int throttle)
{
	if (throttle)
		host_set_single_event(EC_HOST_EVENT_THROTTLE_START);
	else
		host_set_single_event(EC_HOST_EVENT_THROTTLE_STOP);
}

/*
 * Events copy b is used by coreboot for logging the wake reason. For this to
 * work, events_copy_b needs to be cleared on every suspend.
 */
void clear_events_copy_b(void)
{
	events_copy_b = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, clear_events_copy_b, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */
static int command_host_event(int argc, char **argv)
{
	/* Handle sub-commands */
	if (argc == 3) {
		char *e;
		host_event_t i = strtoul(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (!strcasecmp(argv[1], "set"))
			host_set_events(i);
		else if (!strcasecmp(argv[1], "clear"))
			host_clear_events(i);
		else if (!strcasecmp(argv[1], "clearb"))
			host_clear_events_b(i);
#ifdef CONFIG_HOSTCMD_X86
		else if (!strcasecmp(argv[1], "smi"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, i);
		else if (!strcasecmp(argv[1], "sci"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, i);
		else if (!strcasecmp(argv[1], "wake"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, i);
		else if (!strcasecmp(argv[1], "always_report"))
			lpc_set_host_event_mask(LPC_HOST_EVENT_ALWAYS_REPORT,
						i);
#endif
		else
			return EC_ERROR_PARAM1;
	}

	/* Print current SMI/SCI status */
	HOST_EVENT_CCPRINTF("Events:             ", host_get_events());
	HOST_EVENT_CCPRINTF("Events-B:           ", events_copy_b);
#ifdef CONFIG_HOSTCMD_X86
	HOST_EVENT_CCPRINTF("SMI mask:           ",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_SMI));
	HOST_EVENT_CCPRINTF("SCI mask:           ",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_SCI));
	HOST_EVENT_CCPRINTF("Wake mask:          ",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE));
	HOST_EVENT_CCPRINTF("Always report mask: ",
		 lpc_get_host_event_mask(LPC_HOST_EVENT_ALWAYS_REPORT));
#endif
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hostevent, command_host_event,
			"[set | clear | clearb | smi | sci | wake | always_report] [mask]",
			"Print / set host event state");

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_HOSTCMD_X86

static enum ec_status
host_event_get_smi_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r = args->response;

	r->mask = (uint32_t)lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_SMI_MASK,
		     host_event_get_smi_mask,
		     EC_VER_MASK(0));

static enum ec_status
host_event_get_sci_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r = args->response;

	r->mask = (uint32_t)lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_SCI_MASK,
		     host_event_get_sci_mask,
		     EC_VER_MASK(0));

static enum ec_status
host_event_get_wake_mask(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r = args->response;

	r->mask = (uint32_t)lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE);
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_WAKE_MASK,
		     host_event_get_wake_mask,
		     EC_VER_MASK(0));

static enum ec_status
host_event_set_smi_mask(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p = args->params;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_SMI_MASK,
		     host_event_set_smi_mask,
		     EC_VER_MASK(0));

static enum ec_status
host_event_set_sci_mask(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p = args->params;

	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_SCI_MASK,
		     host_event_set_sci_mask,
		     EC_VER_MASK(0));

static enum ec_status
host_event_set_wake_mask(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p = args->params;

	lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, p->mask);
	active_wm_set_by_host = !!p->mask;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_SET_WAKE_MASK,
		     host_event_set_wake_mask,
		     EC_VER_MASK(0));

uint8_t lpc_is_active_wm_set_by_host(void)
{
	return active_wm_set_by_host;
}

#endif  /* CONFIG_HOSTCMD_X86 */

static enum ec_status host_event_get_b(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_mask *r = args->response;

	r->mask = events_copy_b;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_GET_B,
		     host_event_get_b,
		     EC_VER_MASK(0));

static enum ec_status host_event_clear(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p = args->params;

	host_clear_events(p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_CLEAR,
		     host_event_clear,
		     EC_VER_MASK(0));

static enum ec_status host_event_clear_b(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event_mask *p = args->params;

	host_clear_events_b(p->mask);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT_CLEAR_B,
		     host_event_clear_b,
		     EC_VER_MASK(0));

static enum ec_status host_event_action_get(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event *r = args->response;
	const struct ec_params_host_event *p = args->params;
	int result = EC_RES_SUCCESS;

	args->response_size = sizeof(*r);
	memset(r, 0, sizeof(*r));

	switch (p->mask_type) {
	case EC_HOST_EVENT_MAIN:
		result = EC_RES_ACCESS_DENIED;
		break;
	case EC_HOST_EVENT_B:
		r->value = events_copy_b;
		break;
#ifdef CONFIG_HOSTCMD_X86
	case EC_HOST_EVENT_SCI_MASK:
		r->value = lpc_get_host_event_mask(LPC_HOST_EVENT_SCI);
		break;
	case EC_HOST_EVENT_SMI_MASK:
		r->value = lpc_get_host_event_mask(LPC_HOST_EVENT_SMI);
		break;
	case EC_HOST_EVENT_ALWAYS_REPORT_MASK:
		r->value = lpc_get_host_event_mask
				(LPC_HOST_EVENT_ALWAYS_REPORT);
		break;
	case EC_HOST_EVENT_ACTIVE_WAKE_MASK:
		r->value = lpc_get_host_event_mask(LPC_HOST_EVENT_WAKE);
		break;
#ifdef CONFIG_POWER_S0IX
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX:
		r->value = lazy_wm.s0ix_lazy_wm;
		break;
#endif
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S3:
		r->value = lazy_wm.s3_lazy_wm;
		break;
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S5:
		r->value = lazy_wm.s5_lazy_wm;
		break;
#endif
	default:
		result = EC_RES_INVALID_PARAM;
		break;
	}

	return result;
}

static enum ec_status host_event_action_set(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event *p = args->params;
	int result = EC_RES_SUCCESS;
	host_event_t mask_value __unused = (host_event_t)(p->value);

	switch (p->mask_type) {
	case EC_HOST_EVENT_MAIN:
	case EC_HOST_EVENT_B:
		result = EC_RES_ACCESS_DENIED;
		break;
#ifdef CONFIG_HOSTCMD_X86
	case EC_HOST_EVENT_SCI_MASK:
		lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, mask_value);
		break;
	case EC_HOST_EVENT_SMI_MASK:
		lpc_set_host_event_mask(LPC_HOST_EVENT_SMI, mask_value);
		break;
	case EC_HOST_EVENT_ALWAYS_REPORT_MASK:
		lpc_set_host_event_mask(LPC_HOST_EVENT_ALWAYS_REPORT,
						mask_value);
		break;
	case EC_HOST_EVENT_ACTIVE_WAKE_MASK:
		active_wm_set_by_host = !!mask_value;
		lpc_set_host_event_mask(LPC_HOST_EVENT_WAKE, mask_value);
		break;
#ifdef CONFIG_POWER_S0IX
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX:
		lazy_wm.s0ix_lazy_wm = mask_value;
		break;
#endif
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S3:
		lazy_wm.s3_lazy_wm = mask_value;
		break;
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S5:
		lazy_wm.s5_lazy_wm = mask_value;
		break;
#endif
	default:
		result = EC_RES_INVALID_PARAM;
		break;
	}

	return result;
}

static enum ec_status
host_event_action_clear(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event *p = args->params;
	int result = EC_RES_SUCCESS;
	host_event_t mask_value = (host_event_t)(p->value);

	switch (p->mask_type) {
	case EC_HOST_EVENT_MAIN:
		host_clear_events(mask_value);
		break;
	case EC_HOST_EVENT_B:
		host_clear_events_b(mask_value);
		break;
#ifdef CONFIG_HOSTCMD_X86
	case EC_HOST_EVENT_SCI_MASK:
	case EC_HOST_EVENT_SMI_MASK:
	case EC_HOST_EVENT_ALWAYS_REPORT_MASK:
	case EC_HOST_EVENT_ACTIVE_WAKE_MASK:
#ifdef CONFIG_POWER_S0IX
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S0IX:
#endif
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S3:
	case EC_HOST_EVENT_LAZY_WAKE_MASK_S5:
		result = EC_RES_ACCESS_DENIED;
		break;
#endif
	default:
		result = EC_RES_INVALID_PARAM;
	}

	return result;
}

static enum ec_status
host_command_host_event(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_event *p = args->params;

	args->response_size = 0;

	switch (p->action) {
	case EC_HOST_EVENT_GET:
		return host_event_action_get(args);
	case EC_HOST_EVENT_SET:
		return host_event_action_set(args);
	case EC_HOST_EVENT_CLEAR:
		return host_event_action_clear(args);
	default:
		return EC_RES_INVALID_PARAM;
	}
}

DECLARE_HOST_COMMAND(EC_CMD_HOST_EVENT,
		     host_command_host_event,
		     EC_VER_MASK(0));

#define LAZY_WAKE_MASK_SYSJUMP_TAG		0x4C4D /* LM - Lazy Mask*/
#define LAZY_WAKE_MASK_HOOK_VERSION		1

#ifdef CONFIG_HOSTCMD_X86
int get_lazy_wake_mask(enum power_state state, host_event_t *mask)
{
	int ret = EC_SUCCESS;

	switch (state) {
	case POWER_S5:
		*mask = lazy_wm.s5_lazy_wm;
		break;
	case POWER_S3:
		*mask = lazy_wm.s3_lazy_wm;
		break;
#ifdef CONFIG_POWER_S0IX
	case POWER_S0ix:
		*mask = lazy_wm.s0ix_lazy_wm;
		break;
#endif
	default:
		*mask = 0;
		ret = EC_ERROR_INVAL;
	}

	return ret;
}

static void preserve_lazy_wm(void)
{
	system_add_jump_tag(LAZY_WAKE_MASK_SYSJUMP_TAG,
			    LAZY_WAKE_MASK_HOOK_VERSION,
			    sizeof(lazy_wm),
			    &lazy_wm);
}
DECLARE_HOOK(HOOK_SYSJUMP, preserve_lazy_wm, HOOK_PRIO_DEFAULT);

static void restore_lazy_wm(void)
{
	const struct lazy_wake_masks *wm_state;
	int version, size;

	wm_state = (const struct lazy_wake_masks *)
			system_get_jump_tag(LAZY_WAKE_MASK_SYSJUMP_TAG,
				 &version, &size);

	if (wm_state && (version == LAZY_WAKE_MASK_HOOK_VERSION) &&
	    (size == sizeof(lazy_wm))) {
		lazy_wm = *wm_state;
	}
}
DECLARE_HOOK(HOOK_INIT, restore_lazy_wm, HOOK_PRIO_INIT_CHIPSET + 1);
#endif
