/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef USB_TCPMV2_COMPLIANCE_H
#define USB_TCPMV2_COMPLIANCE_H

#define PORT0 0

enum mock_cc_state {
	MOCK_CC_SRC_OPEN = 0,
	MOCK_CC_SNK_OPEN = 0,
	MOCK_CC_SRC_RA = 1,
	MOCK_CC_SNK_RP_DEF = 1,
	MOCK_CC_SRC_RD = 2,
	MOCK_CC_SNK_RP_1_5 = 2,
	MOCK_CC_SNK_RP_3_0 = 3,
};
enum mock_connect_result {
	MOCK_CC_DUT_IS_SRC = 0,
	MOCK_CC_DUT_IS_SNK = 1,
};


extern uint32_t rdo;
extern uint32_t pdo;

extern const struct tcpc_config_t tcpc_config[];
extern const struct usb_mux usb_muxes[];


void mock_set_cc(enum mock_connect_result cr,
	enum mock_cc_state cc1, enum mock_cc_state cc2);
void mock_set_role(int drp, enum tcpc_rp_value rp,
	enum tcpc_cc_pull cc1, enum tcpc_cc_pull cc2);
void mock_set_alert(int alert);
uint16_t tcpc_get_alert_status(void);
bool vboot_allow_usb_pd(void);
int pd_check_vconn_swap(int port);
void board_reset_pd_mcu(void);

int tcpci_startup(void);

void partner_set_data_role(enum pd_data_role data_role);
enum pd_data_role partner_get_data_role(void);

void partner_set_power_role(enum pd_power_role power_role);
enum pd_power_role partner_get_power_role(void);

void partner_set_pd_rev(enum pd_rev_type pd_rev);
enum pd_rev_type partner_get_pd_rev(void);

#define TCPCI_MSG_SOP_ALL -1
void partner_tx_msg_id_reset(int sop);

void partner_send_msg(enum tcpci_msg_type sop,
		      uint16_t type,
		      uint16_t cnt,
		      uint16_t ext,
		      uint32_t *payload);


int handle_attach_expected_msgs(enum pd_data_role data_role);


enum proc_pd_e1_attach {
	INITIAL_ATTACH			= BIT(0),
	ALREADY_ATTACHED		= BIT(1),
	INITIAL_AND_ALREADY_ATTACHED	= INITIAL_ATTACH | ALREADY_ATTACHED
};
int proc_pd_e1(enum pd_data_role data_role, enum proc_pd_e1_attach attach);
int proc_pd_e3(void);

int test_td_pd_ll_e3_dfp(void);
int test_td_pd_ll_e3_ufp(void);
int test_td_pd_ll_e4_dfp(void);
int test_td_pd_ll_e4_ufp(void);
int test_td_pd_ll_e5_dfp(void);
int test_td_pd_ll_e5_ufp(void);

int test_td_pd_src_e1(void);
int test_td_pd_src_e2(void);
int test_td_pd_src_e5(void);

int test_td_pd_src3_e1(void);
int test_td_pd_src3_e7(void);
int test_td_pd_src3_e8(void);
int test_td_pd_src3_e9(void);
int test_td_pd_src3_e26(void);
int test_td_pd_src3_e32(void);

int test_td_pd_snk3_e12(void);

int test_td_pd_vndi3_e3_dfp(void);
int test_td_pd_vndi3_e3_ufp(void);

int test_connect_as_nonpd_sink(void);
int test_retry_count_sop(void);
int test_retry_count_hard_reset(void);

#endif /* USB_TCPMV2_COMPLIANCE_H */
