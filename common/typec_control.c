/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C control logic source */

#include "tcpm/tcpm.h"
#include "typec_control.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"

void typec_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	tcpm_set_polarity(port, polarity);

	if (IS_ENABLED(CONFIG_USBC_PPC_POLARITY))
		ppc_set_polarity(port, polarity);
}

void typec_set_sbu(int port, bool enable)
{
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, enable);

#ifdef CONFIG_USB_PD_TCPM_SBU
	tcpc_set_sbu(port, enable);
#endif
}

__overridable void typec_set_source_current_limit(int port,
						  enum tcpc_rp_value rp)
{
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_set_vbus_source_current_limit(port, rp);
}

void typec_set_vconn(int port, bool enable)
{
	if (!IS_ENABLED(CONFIG_USBC_VCONN))
		return;

	/*
	 * Check our OC event counter.  If we've exceeded our threshold, then
	 * let's latch our source path off to prevent continuous cycling.  When
	 * the PD state machine detects a disconnection on the CC lines, we will
	 * reset our OC event counter.
	 */
	if (IS_ENABLED(CONFIG_USBC_OCP) && enable &&
	    usbc_ocp_is_port_latched_off(port))
		return;

	/*
	 * Disable PPC Vconn first then TCPC in case the voltage feeds back
	 * to TCPC and damages.
	 */
	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN) && !enable)
		ppc_set_vconn(port, false);

	/*
	 * Some TCPCs/PPC combinations can trigger OVP if the TCPC doesn't
	 * source VCONN. This happens if the TCPC will trip OVP with 5V, and the
	 * PPC doesn't isolate the TCPC from VCONN when sourcing. But, some PPCs
	 * which do isolate the TCPC can't handle 5V on its host-side CC pins,
	 * so the TCPC shouldn't source VCONN in those cases.
	 *
	 * In the first case, both TCPC and PPC will potentially source Vconn,
	 * but that should be okay since Vconn has "make before break"
	 * electrical requirements when swapping anyway.
	 *
	 * See b/72961003 and b/180973460
	 */
	tcpm_set_vconn(port, enable);

	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN) && enable)
		ppc_set_vconn(port, true);
}
