/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "driver/tcpm/tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_ext.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "emul/tcpc/emul_tcpci_partner_vpd.h"

LOG_MODULE_REGISTER(tcpci_vpd_emul, CONFIG_TCPCI_EMUL_LOG_LEVEL);

static enum tcpci_partner_handler_res
tcpci_vpd_emul_handle_sop_msg(struct tcpci_partner_extension *ext,
			      struct tcpci_partner_data *common_data,
			      const struct tcpci_emul_msg *msg)
{
	struct tcpci_vpd_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_vpd_emul_data, ext);

	/* Do not respond to SOP messages until charge-through is connected. */
	if (!data->charge_through_connected)
		return TCPCI_PARTNER_COMMON_MSG_NO_GOODCRC;

	/* Once charge-through is connected, let the charger (later extension)
	 * handle SOP messages.
	 */
	return TCPCI_PARTNER_COMMON_MSG_NOT_HANDLED;
}

static int tcpci_vpd_emul_connect(struct tcpci_partner_extension *ext,
				  struct tcpci_partner_data *common_data)
{
	struct tcpci_vpd_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_vpd_emul_data, ext);

	/* Strictly speaking, the VPD shouldn't GoodCRC anything on SOP, Source
	 * Capabilities is the first message it will receive, so that's good
	 * enough.
	 */
	if (common_data->power_role == PD_ROLE_SOURCE) {
		tcpci_faulty_ext_clear_actions_list(&data->fault_ext);
	} else {
		data->fault_actions[0].action_mask =
			TCPCI_FAULTY_EXT_FAIL_SRC_CAP;
		data->fault_actions[0].count = TCPCI_FAULTY_EXT_INFINITE_ACTION;
		tcpci_faulty_ext_append_action(&data->fault_ext,
					       &data->fault_actions[0]);
	}

	return 0;
}

static void
tcpci_vpd_emul_control_change(struct tcpci_partner_extension *ext,
			      struct tcpci_partner_data *common_data)
{
	struct tcpci_vpd_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_vpd_emul_data, ext);
	enum pd_cc_states tcpm_cc_state =
		pd_get_cc_state(common_data->tcpm_cc1, common_data->tcpm_cc2);

	LOG_DBG("TCPM changed control register; TCPM CC1 %u, CC2 %u",
		common_data->tcpm_cc1, common_data->tcpm_cc2);
	if (!data->charge_through_connected &&
	    tcpm_cc_state == PD_CC_UFP_ATTACHED) {
		LOG_INF("Connecting charge-through port as Source");
		data->charge_through_connected = true;
		tcpci_partner_common_hard_reset_as_role(common_data,
							PD_ROLE_SOURCE);
		tcpci_partner_connect_to_tcpci(common_data,
					       common_data->tcpci_emul);
	}
}

void tcpci_vpd_emul_disconnect(struct tcpci_partner_extension *ext,
			       struct tcpci_partner_data *common_data)
{
	struct tcpci_vpd_emul_data *data =
		CONTAINER_OF(ext, struct tcpci_vpd_emul_data, ext);

	data->charge_through_connected = false;
	tcpci_partner_common_hard_reset_as_role(common_data, PD_ROLE_SINK);
}

static struct tcpci_partner_extension_ops vpd_emul_ops = {
	.sop_msg_handler = tcpci_vpd_emul_handle_sop_msg,
	.connect = tcpci_vpd_emul_connect,
	.control_change = tcpci_vpd_emul_control_change,
	.disconnect = tcpci_vpd_emul_disconnect,
};

struct tcpci_partner_extension *
tcpci_vpd_emul_init(struct tcpci_vpd_emul_data *data,
		    struct tcpci_partner_data *common_data,
		    struct tcpci_partner_extension *ext)
{
	struct tcpci_partner_extension *vpd_ext = &data->ext;
	struct tcpci_partner_extension *snk_ext;
	struct tcpci_partner_extension *src_ext;

	/* A VPD host port initially attaches as a Sink and responds to SOP'
	 * Discover Identity while ignoring SOP traffic. Then, when a Source is
	 * connected to the charge-through port, the CT-VPD acts as a Source.
	 * This extension therefore contains a faulty extension, a sink
	 * extension, and a source extension, in that order. Due to the
	 * linked-list extension structure, the initialization order is the
	 * reverse of that.
	 */
	src_ext = tcpci_src_emul_init(&data->src_ext, common_data, ext);
	snk_ext = tcpci_snk_emul_init(&data->snk_ext, common_data, src_ext);
	vpd_ext->next =
		tcpci_faulty_ext_init(&data->fault_ext, common_data, snk_ext);

	vpd_ext->ops = &vpd_emul_ops;

	data->charge_through_connected = false;

	return vpd_ext;
}
