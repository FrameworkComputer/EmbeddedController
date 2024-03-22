/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C module.
 */
#include "common.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_tc_sm.h"

__overridable int pd_is_vbus_present(int port)
{
	return 0;
}

__overridable void pd_request_data_swap(int port)
{
}

__overridable void pd_request_power_swap(int port)
{
}

void pd_request_vconn_swap_off(int port)
{
}

void pd_request_vconn_swap_on(int port)
{
}

static enum pd_data_role data_role;
__overridable enum pd_data_role pd_get_data_role(int port)
{
	return data_role;
}
__overridable void tc_set_data_role(int port, enum pd_data_role role)
{
	data_role = role;
}

static enum pd_power_role power_role;
__overridable enum pd_power_role pd_get_power_role(int port)
{
	return power_role;
}
__overridable void tc_set_power_role(int port, enum pd_power_role role)
{
	power_role = role;
}

__overridable bool pd_get_partner_usb_comm_capable(int port)
{
	return true;
}

__overridable enum pd_cable_plug tc_get_cable_plug(int port)
{
	return PD_PLUG_FROM_DFP_UFP;
}

__overridable int pd_get_rev(int port, enum tcpci_msg_type type)
{
	return IS_ENABLED(CONFIG_USB_PD_REV30) ? PD_REV30 : PD_REV20;
}

int tc_check_vconn_swap(int port)
{
	return 0;
}

void tc_ctvpd_detected(int port)
{
}

void tc_disc_ident_complete(int port)
{
}

static int attached_snk;
int tc_is_attached_snk(int port)
{
	return attached_snk;
}

static int attached_src;
int tc_is_attached_src(int port)
{
	return attached_src;
}

int tc_is_vconn_src(int port)
{
	return 0;
}

void tc_hard_reset_request(int port)
{
}

void tc_hard_reset_complete(int port)
{
}

void tc_partner_dr_data(int port, int en)
{
}

void tc_partner_dr_power(int port, int en)
{
}

void tc_partner_unconstrainedpower(int port, int en)
{
}

void tc_partner_usb_comm(int port, int en)
{
}

void tc_pd_connection(int port, int en)
{
}

void tc_pr_swap_complete(int port, bool success)
{
}

void tc_prs_snk_src_assert_rp(int port)
{
	attached_snk = 0;
	attached_src = 1;
}

void tc_prs_src_snk_assert_rd(int port)
{
	attached_snk = 1;
	attached_src = 0;
}

void tc_src_power_off(int port)
{
}

void tc_set_timeout(int port, uint64_t timeout)
{
}

__overridable void tc_start_error_recovery(int port)
{
}

__overridable void tc_snk_power_off(int port)
{
}

__overridable void pe_invalidate_explicit_contract(int port)
{
}

__overridable enum pd_dual_role_states pd_get_dual_role(int port)
{
	return PD_DRP_TOGGLE_ON;
}

__overridable void pd_dev_get_rw_hash(int port, uint16_t *dev_id,
				      uint8_t *rw_hash, uint32_t *current_image)
{
}

__overridable int pd_dev_store_rw_hash(int port, uint16_t dev_id,
				       uint32_t *rw_hash,
				       uint32_t current_image)
{
	return 0;
}

__overridable int pd_comm_is_enabled(int port)
{
	return 0;
}

bool pd_get_partner_data_swap_capable(int port)
{
	return true;
}

bool pd_capable(int port)
{
	return true;
}

#ifndef CONFIG_TEST_USB_PE_SM
enum idh_ptype get_usb_pd_mux_cable_type(int port)
{
	return IDH_PTYPE_UNDEF;
}

const uint32_t *const pd_get_src_caps(int port)
{
	return NULL;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}
#endif

#if !defined(CONFIG_USB_DRP_ACC_TRYSRC) && !defined(CONFIG_USB_CTVPD)
int pd_is_connected(int port)
{
	return true;
}

bool pd_is_disconnected(int port)
{
	return false;
}
#endif /* !CONFIG_USB_DRP_ACC_TRYSRC && !CONFIG_USB_CTVPD */

#ifndef CONFIG_USB_DRP_ACC_TRYSRC
__overridable void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
}

__overridable enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return POLARITY_CC1;
}

bool pd_get_vconn_state(int port)
{
	return false;
}

bool pd_get_partner_dual_role_power(int port)
{
	return false;
}

uint8_t pd_get_task_state(int port)
{
	return 0;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return PD_CC_NONE;
}

bool pd_get_partner_unconstr_power(int port)
{
	return 0;
}

const char *pd_get_task_state_name(int port)
{
	return NULL;
}
#endif /* CONFIG_USB_DRP_ACC_TRYSRC */

void dfp_consume_attention(int port, uint32_t *payload)
{
}

void pd_prepare_sysjump(void)
{
}

void dpm_init(int port)
{
}

void dpm_mode_exit_complete(int port)
{
}

void dpm_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		   uint32_t *vdm)
{
}

void dpm_vdm_naked(int port, enum tcpci_msg_type type, uint16_t svid,
		   uint8_t vdm_cmd, uint32_t vdm_header)
{
}

void dpm_set_mode_entry_done(int port)
{
}

void dpm_set_mode_exit_request(int port)
{
}

void dpm_notify_attention(int port, size_t vdo_objects, uint32_t *buf)
{
}

void dpm_run(int port, int evt, int enable)
{
}

void dpm_evaluate_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo)
{
}

void dpm_add_non_pd_sink(int port)
{
}

void dpm_evaluate_request_rdo(int port, uint32_t rdo)
{
}

void dpm_remove_sink(int port)
{
}

void dpm_remove_source(int port)
{
}

void dpm_bist_shared_mode_enter(int port)
{
}

void dpm_bist_shared_mode_exit(int port)
{
}

int dpm_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnt;
}

int dpm_get_status_msg(int port, uint8_t *msg, uint32_t *len)
{
	return EC_SUCCESS;
}

void dpm_handle_alert(int port, uint32_t ado)
{
}

void dpm_set_pe_ready(int port, bool enable)
{
}

static enum tcpc_rp_value lcl_rp;
__overridable void typec_select_src_current_limit_rp(int port,
						     enum tcpc_rp_value rp)
{
	lcl_rp = rp;
}
__overridable void typec_select_src_collision_rp(int port,
						 enum tcpc_rp_value rp)
{
	lcl_rp = rp;
}
__overridable int typec_update_cc(int port)
{
	return EC_SUCCESS;
}
