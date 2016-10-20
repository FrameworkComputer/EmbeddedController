/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for PD MCU */

#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "lightbar.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_PD_HOST_CMD, format, ## args)

#define TASK_EVENT_EXCHANGE_PD_STATUS  TASK_EVENT_CUSTOM(1)
#define TASK_EVENT_HIBERNATING         TASK_EVENT_CUSTOM(2)

/* Define local option for if we are a TCPM with an off chip TCPC */
#if defined(CONFIG_USB_POWER_DELIVERY) && !defined(CONFIG_USB_PD_TCPM_STUB)
#define USB_TCPM_WITH_OFF_CHIP_TCPC
#endif

#ifdef CONFIG_HOSTCMD_PD_CHG_CTRL
/* By default allow 5V charging only for the dead battery case */
static enum pd_charge_state charge_state = PD_CHARGE_5V;

#define CHARGE_PORT_UNINITIALIZED -2
static int charge_port = CHARGE_PORT_UNINITIALIZED;

int pd_get_active_charge_port(void)
{
	return charge_port;
}
#endif /* CONFIG_HOSTCMD_PD_CHG_CTRL */

void host_command_pd_send_status(enum pd_charge_state new_chg_state)
{
#ifdef CONFIG_HOSTCMD_PD_CHG_CTRL
	/* Update PD MCU charge state if necessary */
	if (new_chg_state != PD_CHARGE_NO_CHANGE)
		charge_state = new_chg_state;
#endif
	/* Wake PD HC task to send status */
	task_set_event(TASK_ID_PDCMD, TASK_EVENT_EXCHANGE_PD_STATUS, 0);
}

void host_command_pd_request_hibernate(void)
{
	task_set_event(TASK_ID_PDCMD, TASK_EVENT_HIBERNATING, 0);
}

#ifdef CONFIG_HOSTCMD_PD
static int pd_send_host_command(struct ec_params_pd_status *ec_status,
	struct ec_response_pd_status *pd_status)
{
	return pd_host_command(EC_CMD_PD_EXCHANGE_STATUS,
			       EC_VER_PD_EXCHANGE_STATUS, ec_status,
			       sizeof(struct ec_params_pd_status), pd_status,
			       sizeof(struct ec_response_pd_status));
}

static void pd_exchange_update_ec_status(struct ec_params_pd_status *ec_status,
					 uint32_t ec_state)
{
	/* Send PD charge state and battery state of charge */
#ifdef CONFIG_HOSTCMD_PD_CHG_CTRL
	ec_status->charge_state = charge_state;
#endif
	if (charge_get_flags() & CHARGE_FLAG_BATT_RESPONSIVE)
		ec_status->batt_soc = charge_get_percent();
	else
		ec_status->batt_soc = -1;
	ec_status->status = ec_state;
}

#ifdef CONFIG_HOSTCMD_PD_PANIC
static void pd_check_panic(struct ec_response_pd_status *pd_status)
{
	static int pd_in_rw;

	/*
	 * Check if PD MCU is in RW. If PD MCU was in RW, is now in RO,
	 * AND it did not sysjump to RO, then it must have crashed, and
	 * therefore we should panic as well.
	 */
	if (pd_status->status & PD_STATUS_IN_RW) {
		pd_in_rw = 1;
	} else if (pd_in_rw &&
		   !(pd_status->status & PD_STATUS_JUMPED_TO_IMAGE)) {
		panic_printf("PD crash");
		software_panic(PANIC_SW_PD_CRASH, 0);
	}
}
#endif /* CONFIG_HOSTCMD_PD_PANIC */

#ifdef CONFIG_HOSTCMD_PD_CHG_CTRL
static void pd_check_chg_status(struct ec_response_pd_status *pd_status)
{
	int rv;
#ifdef HAS_TASK_LIGHTBAR
	/*
	 * If charge port has changed, and it was initialized, then show
	 * battery status on lightbar.
	 */
	if (pd_status->active_charge_port != charge_port) {
		if (charge_port != CHARGE_PORT_UNINITIALIZED) {
			charge_port = pd_status->active_charge_port;
			lightbar_sequence(LIGHTBAR_TAP);
		} else {
			charge_port = pd_status->active_charge_port;
		}
	}
#else
	/* Store the active charge port */
	charge_port = pd_status->active_charge_port;
#endif

	/* Set input current limit */
	rv = charge_set_input_current_limit(MAX(pd_status->curr_lim_ma,
					CONFIG_CHARGER_INPUT_CURRENT));
	if (rv < 0)
		CPRINTS("Failed to set input curr limit from PD MCU");
}
#endif /* CONFIG_HOSTCMD_PD_CHG_CTRL */
#endif /* CONFIG_HOSTCMD_PD */

#ifdef USB_TCPM_WITH_OFF_CHIP_TCPC
static void pd_service_tcpc_ports(uint16_t port_status)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		if (port_status & (PD_STATUS_TCPC_ALERT_0 << i))
			tcpc_alert(i);
	}
}

static int pd_get_alert(void)
{
#ifdef CONFIG_HOSTCMD_PD
	return !gpio_get_level(GPIO_PD_MCU_INT);
#else
	return !!tcpc_get_alert_status();
#endif
}

#endif /* USB_TCPM_WITH_OFF_CHIP_TCPC */

static void pd_exchange_status(uint32_t ec_state)
{
#ifdef USB_TCPM_WITH_OFF_CHIP_TCPC
	int first_exchange = 1;
#endif

#ifdef CONFIG_HOSTCMD_PD
	struct ec_params_pd_status ec_status;
	struct ec_response_pd_status pd_status;
	int rv;

	pd_exchange_update_ec_status(&ec_status, ec_state);
#endif

#ifdef USB_TCPM_WITH_OFF_CHIP_TCPC
	/* Loop until the alert gpio is not active */
	do {
#endif

#ifdef CONFIG_HOSTCMD_PD
		rv = pd_send_host_command(&ec_status, &pd_status);
		if (rv < 0) {
			CPRINTS("Host command to PD MCU failed: %d", rv);
			return;
		}

#ifdef CONFIG_HOSTCMD_PD_PANIC
		pd_check_panic(&pd_status);
#endif

#ifdef CONFIG_HOSTCMD_PD_CHG_CTRL
		pd_check_chg_status(&pd_status);
#endif
#endif /* CONFIG_HOSTCMD_PD */

#ifdef USB_TCPM_WITH_OFF_CHIP_TCPC
#ifdef CONFIG_HOSTCMD_PD
		pd_service_tcpc_ports(pd_status.status);
#else
		pd_service_tcpc_ports(tcpc_get_alert_status());
#endif

		if (!first_exchange)
			usleep(50*MSEC);
		first_exchange = 0;
	} while (pd_get_alert());
#endif /* USB_TCPM_WITH_OFF_CHIP_TCPC */
}

void pd_command_task(void)
{
	/* On startup exchange status with the PD */
	pd_exchange_status(0);

	while (1) {
		/* Wait for the next command event */
		int evt = task_wait_event(-1);
		uint32_t ec_state = 0;

		if (evt & TASK_EVENT_HIBERNATING)
			ec_state = EC_STATUS_HIBERNATING;

		/* Process event to send status to PD */
		if ((evt & TASK_EVENT_EXCHANGE_PD_STATUS) ||
		    (evt & TASK_EVENT_HIBERNATING))
			pd_exchange_status(ec_state);
	}
}

#ifdef USB_TCPM_WITH_OFF_CHIP_TCPC
/*
 * PD host event status for host command
 * Note: this variable must be aligned on 4-byte boundary because we pass the
 * address to atomic_ functions which use assembly to access them.
 */
static uint32_t pd_host_event_status __aligned(4);

static int hc_pd_host_event_status(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Read and clear the host event status to return to AP */
	r->status = atomic_read_clear(&pd_host_event_status);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, hc_pd_host_event_status,
		     EC_VER_MASK(0));

/* Send host event up to AP */
void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	atomic_or(&pd_host_event_status, mask);
	/* interrupt the AP */
	host_set_single_event(EC_HOST_EVENT_PD_MCU);
}
#endif
