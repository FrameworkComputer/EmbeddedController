/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "board.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_prl_sm.h"
#include "tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"
#include "version.h"

/* Include USB Type-C State Machine Header File */
#if defined(CONFIG_USB_TYPEC_CTVPD)
#include "usb_tc_ctvpd_sm.h"
#elif defined(CONFIG_USB_TYPEC_VPD)
#include "usb_tc_vpd_sm.h"
#elif defined(CONFIG_USB_TYPEC_DRP_ACC_TRYSRC)
#include "usb_tc_drp_acc_trysrc_sm.h"
#else
#error "A USB Type-C State Machine must be defined."
#endif

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#ifdef CONFIG_COMMON_RUNTIME
const char * const tc_state_names[] = {
	"Disabled",
	"Unattached.SNK",
	"AttachWait.SNK",
	"Attached.SNK",
#if !defined(CONFIG_USB_TYPEC_VPD)
	"ErrorRecovery",
	"Unattached.SRC",
	"AttachWait.SRC",
	"Attached.SRC",
#endif
#if !defined(CONFIG_USB_TYPEC_CTVPD) && !defined(CONFIG_USB_TYPEC_VPD)
	"AudioAccessory",
	"OrientedDebugAccessory.SRC",
	"UnorientedDebugAccessory.SRC",
	"DebugAccessory.SNK",
	"Try.SRC",
	"TryWait.SNK",
	"CTUnattached.SNK",
	"CTAttached.SNK",
#endif
#if defined(CONFIG_USB_TYPEC_CTVPD)
	"CTTry.SNK",
	"CTAttached.Unsupported",
	"CTAttachWait.Unsupported",
	"CTUnattached.Unsupported",
	"CTUnattached.VPD",
	"CTAttachWait.VPD",
	"CTAttached.VPD",
	"CTDisabled.VPD",
	"Try.SNK",
	"TryWait.SRC"
#endif
};
BUILD_ASSERT(ARRAY_SIZE(tc_state_names) == TC_STATE_COUNT);
#endif

/* Public Functions */

int tc_get_power_role(int port)
{
	return tc[port].power_role;
}

int tc_get_data_role(int port)
{
	return tc[port].data_role;
}

void tc_set_power_role(int port, int role)
{
	tc[port].power_role = role;
}

void tc_set_timeout(int port, uint64_t timeout)
{
	tc[port].evt_timeout = timeout;
}

enum typec_state_id get_typec_state_id(int port)
{
	return tc[port].state_id;
}

/*
 * CC values for regular sources and Debug sources (aka DTS)
 *
 * Source type  Mode of Operation   CC1    CC2
 * ---------------------------------------------
 * Regular      Default USB Power   RpUSB  Open
 * Regular      USB-C @ 1.5 A       Rp1A5  Open
 * Regular      USB-C @ 3 A         Rp3A0  Open
 * DTS          Default USB Power   Rp3A0  Rp1A5
 * DTS          USB-C @ 1.5 A       Rp1A5  RpUSB
 * DTS          USB-C @ 3 A         Rp3A0  RpUSB
 */

enum pd_cc_polarity_type get_snk_polarity(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2)
{
	/* the following assumes:
	 * TYPEC_CC_VOLT_RP_3_0 > TYPEC_CC_VOLT_RP_1_5
	 * TYPEC_CC_VOLT_RP_1_5 > TYPEC_CC_VOLT_RP_DEF
	 * TYPEC_CC_VOLT_RP_DEF > TYPEC_CC_VOLT_OPEN
	 */
	return (cc2 > cc1) ? POLARITY_CC2 : POLARITY_CC1;
}

int tc_restart_tcpc(int port)
{
	return tcpm_init(port);
}

void set_polarity(int port, int polarity)
{
	tcpm_set_polarity(port, polarity);

	if (IS_ENABLED(CONFIG_USBC_PPC_POLARITY))
		ppc_set_polarity(port, polarity);
}

void set_usb_mux_with_current_data_role(int port)
{
#ifdef CONFIG_USBC_SS_MUX
	/*
	 * If the SoC is down, then we disconnect the MUX to save power since
	 * no one cares about the data lines.
	 */
#ifdef CONFIG_POWER_COMMON
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF)) {
		usb_mux_set(port, TYPEC_MUX_NONE, USB_SWITCH_DISCONNECT,
			tc[port].polarity);
		return;
	}
#endif /* CONFIG_POWER_COMMON */

	/*
	 * When PD stack is disconnected, then mux should be disconnected, which
	 * is also what happens in the set_state disconnection code. Once the
	 * PD state machine progresses out of disconnect, the MUX state will
	 * be set correctly again.
	 */
	if (!pd_is_connected(port))
		usb_mux_set(port, TYPEC_MUX_NONE, USB_SWITCH_DISCONNECT,
			tc[port].polarity);
	/*
	 * If new data role isn't DFP and we only support DFP, also disconnect.
	 */
	else if (IS_ENABLED(CONFIG_USBC_SS_MUX_DFP_ONLY) &&
			tc[port].data_role != PD_ROLE_DFP)
		usb_mux_set(port, TYPEC_MUX_NONE, USB_SWITCH_DISCONNECT,
			tc[port].polarity);
	/*
	 * Otherwise connect mux since we are in S3+
	 */
	else
		usb_mux_set(port, TYPEC_MUX_USB, USB_SWITCH_CONNECT,
			tc[port].polarity);
#endif /* CONFIG_USBC_SS_MUX */
}

/* High-priority interrupt tasks implementations */
#if     defined(HAS_TASK_PD_INT_C0) || defined(HAS_TASK_PD_INT_C1) || \
	defined(HAS_TASK_PD_INT_C2)

/* Used to conditionally compile code in main pd task. */
#define HAS_DEFFERED_INTERRUPT_HANDLER

/* Events for pd_interrupt_handler_task */
#define PD_PROCESS_INTERRUPT  (1<<0)

static uint8_t pd_int_task_id[CONFIG_USB_PD_PORT_COUNT];

void schedule_deferred_pd_interrupt(const int port)
{
	task_set_event(pd_int_task_id[port], PD_PROCESS_INTERRUPT, 0);
}

/*
 * Main task entry point that handles PD interrupts for a single port
 *
 * @param p The PD port number for which to handle interrupts (pointer is
 * reinterpreted as an integer directly).
 */
void pd_interrupt_handler_task(void *p)
{
	const int port = (int) p;
	const int port_mask = (PD_STATUS_TCPC_ALERT_0 << port);

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_COUNT);

	pd_int_task_id[port] = task_get_current();

	while (1) {
		const int evt = task_wait_event(-1);

		if (evt & PD_PROCESS_INTERRUPT) {
			/*
			 * While the interrupt signal is asserted; we have more
			 * work to do. This effectively makes the interrupt a
			 * level-interrupt instead of an edge-interrupt without
			 * having to enable/disable a real level-interrupt in
			 * multiple locations.
			 *
			 * Also, if the port is disabled do not process
			 * interrupts. Upon existing suspend, we schedule a
			 * PD_PROCESS_INTERRUPT to check if we missed anything.
			 */
			while ((tcpc_get_alert_status() & port_mask) &&
					pd_is_port_enabled(port))
				tcpc_alert(port);
		}
	}
}
#endif /* HAS_TASK_PD_INT_C0 || HAS_TASK_PD_INT_C1 || HAS_TASK_PD_INT_C2 */

void pd_task(void *u)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	tc_state_init(port, TC_DEFAULT_STATE(port));

	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_init(port);

	/*
	 * Since most boards configure the TCPC interrupt as edge
	 * and it is possible that the interrupt line was asserted between init
	 * and calling set_state, we need to process any pending interrupts now.
	 * Otherwise future interrupts will never fire because another edge
	 * never happens. Note this needs to happen after set_state() is called.
	 */
	if (IS_ENABLED(HAS_DEFFERED_INTERRUPT_HANDLER))
		schedule_deferred_pd_interrupt(port);


	while (1) {
		/* wait for next event/packet or timeout expiration */
		tc[port].evt = task_wait_event(tc[port].evt_timeout);

		/* handle events that affect the state machine as a whole */
		tc_event_check(port, tc[port].evt);

#ifdef CONFIG_USB_PD_TCPC
		/*
		 * run port controller task to check CC and/or read incoming
		 * messages
		 */
		tcpc_run(port, tc[port].evt);
#endif

#ifdef CONFIG_USB_PE_SM
		/* run policy engine state machine */
		usbc_policy_engine(port, tc[port].evt, tc[port].pd_enable);
#endif

#ifdef CONFIG_USB_PRL_SM
		/* run protocol state machine */
		usbc_protocol_layer(port, tc[port].evt, tc[port].pd_enable);
#endif

		/* run typec state machine */
		sm_run_state_machine(port, TC_OBJ(port), SM_RUN_SIG);
	}
}
