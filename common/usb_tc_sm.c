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
#include "usb_pd_tcpm.h"
#include "usb_prl_sm.h"
#include "tcpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "version.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_HOOK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_HOOK, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Private Function Prototypes */

static inline int cc_is_rp(int cc);
static inline enum pd_cc_polarity_type get_snk_polarity(int cc1, int cc2);
static int tc_restart_tcpc(int port);
static void set_polarity(int port, int polarity);

#ifdef CONFIG_COMMON_RUNTIME
static const char * const tc_state_names[] = {
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

/* Include USB Type-C State Machine */
#if defined(CONFIG_USB_TYPEC_CTVPD)
#include "usb_tc_ctvpd_sm.h"
#elif defined(CONFIG_USB_TYPEC_VPD)
#include "usb_tc_vpd_sm.h"
#else
#error "A USB Type-C State Machine must be defined."
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

void tc_set_timeout(int port, uint64_t timeout)
{
	tc[port].evt_timeout = timeout;
}

enum typec_state_id get_typec_state_id(int port)
{
	return tc[port].state_id;
}

/* Private Functions */

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

/**
 * Returns the polarity of a Sink.
 */
static inline enum pd_cc_polarity_type get_snk_polarity(int cc1, int cc2)
{
	/* the following assumes:
	 * TYPEC_CC_VOLT_RP_3_0 > TYPEC_CC_VOLT_RP_1_5
	 * TYPEC_CC_VOLT_RP_1_5 > TYPEC_CC_VOLT_RP_DEF
	 * TYPEC_CC_VOLT_RP_DEF > TYPEC_CC_VOLT_OPEN
	 */
	return (cc2 > cc1) ? POLARITY_CC2 : POLARITY_CC1;
}

static int tc_restart_tcpc(int port)
{
	return tcpm_init(port);
}

static void set_polarity(int port, int polarity)
{
	tcpm_set_polarity(port, polarity);
#ifdef CONFIG_USBC_PPC_POLARITY
	ppc_set_polarity(port, polarity);
#endif /* defined(CONFIG_USBC_PPC_POLARITY) */
}

void pd_task(void *u)
{
	int port = TASK_ID_TO_PD_PORT(task_get_current());

	tc_state_init(port);

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
		policy_engine(port, tc[port].evt, tc[port].pd_enable);
#endif /* CONFIG_USB_PE_SM */

#ifdef CONFIG_USB_PRL_SM
		/* run protocol state machine */
		protocol_layer(port, tc[port].evt, tc[port].pd_enable);
#endif /* CONFIG_USB_PRL_SM */

		/* run state machine */
		exe_state(port, TC_OBJ(port), RUN_SIG);
	}
}
