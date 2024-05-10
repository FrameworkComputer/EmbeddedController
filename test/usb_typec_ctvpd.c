/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C VPD and CTVPD module.
 */
#include "common.h"
#include "crc.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_test_util.h"
#include "usb_sm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "vpd_api.h"

#define PORT0 0

enum cc_type { CC1, CC2 };
enum vbus_type { VBUS_0 = 0, VBUS_5 = 5000 };
enum vconn_type { VCONN_0 = 0, VCONN_3 = 3000, VCONN_5 = 5000 };
enum snk_con_voltage_type { SRC_CON_DEF, SRC_CON_1_5, SRC_CON_3_0 };

/*
 * These enum definitions are declared in usb_tc_*_sm and are private to that
 * file. If those definitions are re-ordered, then we need to update these
 * definitions (should be very rare).
 */
enum usb_tc_state {
	/* Normal States */
	TC_DISABLED,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	TC_ERROR_RECOVERY,
	TC_TRY_SNK,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_TRY_WAIT_SRC,
	TC_ATTACHED_SRC,
	TC_CT_TRY_SNK,
	TC_CT_ATTACH_WAIT_UNSUPPORTED,
	TC_CT_ATTACHED_UNSUPPORTED,
	TC_CT_UNATTACHED_UNSUPPORTED,
	TC_CT_UNATTACHED_VPD,
	TC_CT_DISABLED_VPD,
	TC_CT_ATTACHED_VPD,
	TC_CT_ATTACH_WAIT_VPD,
};

/* Defined in implementation */
enum usb_tc_state get_state_tc(const int port);

struct pd_port_t {
	int host_mode;
	int has_vbus;
	int msg_tx_id;
	int msg_rx_id;
	int polarity;
	int partner_role; /* -1 for none */
	int partner_polarity;
	int rev;
} pd_port[CONFIG_USB_PD_PORT_MAX_COUNT];

uint64_t wait_for_state_change(int port, uint64_t timeout)
{
	uint64_t start;
	uint64_t wait;
	enum usb_tc_state state = get_state_tc(port);

	task_wake(PD_PORT_TO_TASK_ID(port));

	wait = get_time().val + timeout;
	start = get_time().val;
	while (get_state_tc(port) == state && get_time().val < wait) {
		task_wake(PD_PORT_TO_TASK_ID(port));
		task_wait_event(1 * MSEC);
	}

	return get_time().val - start;
}

#if defined(TEST_USB_TYPEC_CTVPD)
static int ct_connect_sink(enum cc_type cc, enum snk_con_voltage_type v)
{
	int ret;

	switch (v) {
	case SRC_CON_DEF:
		ret = (cc) ? mock_set_cc2_rp3a0_rd_l(PD_SRC_DEF_RD_THRESH_MV) :
			     mock_set_cc1_rp3a0_rd_l(PD_SRC_DEF_RD_THRESH_MV);
		break;
	case SRC_CON_1_5:
		ret = (cc) ? mock_set_cc2_rp3a0_rd_l(PD_SRC_1_5_RD_THRESH_MV) :
			     mock_set_cc1_rp3a0_rd_l(PD_SRC_1_5_RD_THRESH_MV);
		break;
	case SRC_CON_3_0:
		ret = (cc) ? mock_set_cc2_rp3a0_rd_l(PD_SRC_3_0_RD_THRESH_MV) :
			     mock_set_cc1_rp3a0_rd_l(PD_SRC_3_0_RD_THRESH_MV);
		break;
	default:
		ret = 0;
	}

	return ret;
}

static int ct_disconnect_sink(void)
{
	int r1;
	int r2;

	r1 = mock_set_cc1_rp3a0_rd_l(PD_SRC_DEF_VNC_MV);
	r2 = mock_set_cc2_rp3a0_rd_l(PD_SRC_DEF_VNC_MV);

	return r1 & r2;
}

static int ct_connect_source(enum cc_type cc, enum vbus_type vbus)
{
	mock_set_ct_vbus(vbus);
	return (cc) ? mock_set_cc2_rpusb_odh(PD_SNK_VA_MV) :
		      mock_set_cc1_rpusb_odh(PD_SNK_VA_MV);
}

static int ct_disconnect_source(void)
{
	int r1;
	int r2;

	mock_set_ct_vbus(VBUS_0);
	r1 = mock_set_cc1_rpusb_odh(0);
	r2 = mock_set_cc2_rpusb_odh(0);

	return r1 & r2;
}
#endif

static void host_disconnect_source(void)
{
	mock_set_host_vbus(VBUS_0);
	mock_set_host_cc_source_voltage(0);
	mock_set_host_cc_sink_voltage(0);
}

static void host_connect_source(enum vbus_type vbus)
{
	mock_set_host_vbus(vbus);
	mock_set_host_cc_source_voltage(PD_SNK_VA_MV);
}

#if defined(TEST_USB_TYPEC_CTVPD)
static void host_connect_sink(enum snk_con_voltage_type v)
{
	switch (v) {
	case SRC_CON_DEF:
		mock_set_host_cc_sink_voltage(PD_SRC_DEF_RD_THRESH_MV);
		break;
	case SRC_CON_1_5:
		mock_set_host_cc_sink_voltage(PD_SRC_1_5_RD_THRESH_MV);
		break;
	case SRC_CON_3_0:
		mock_set_host_cc_sink_voltage(PD_SRC_3_0_RD_THRESH_MV);
		break;
	}
}
#endif

static void init_port(int port)
{
	pd_port[port].polarity = 0;
	pd_port[port].rev = PD_REV30;
	pd_port[port].msg_tx_id = 0;
	pd_port[port].msg_rx_id = 0;
}

static int check_host_ra_rd(void)
{
	/* Make sure CC_RP3A0_RD_L is configured as GPO */
	if (mock_get_cfg_cc_rp3a0_rd_l() != PIN_GPO)
		return 0;

	/* Make sure CC_RP3A0_RD_L is asserted low */
	if (mock_get_cc_rp3a0_rd_l() != 0)
		return 0;

	/* Make sure VPDMCU_CC_EN is enabled */
	if (mock_get_mcu_cc_en() != 1)
		return 0;

	/* Make sure CC_VPDMCU is configured as ADC */
	if (mock_get_cfg_cc_vpdmcu() != PIN_ADC)
		return 0;

	/* Make sure CC_DB_EN_OD is HZ */
	if (mock_get_cc_db_en_od() != GPO_HZ)
		return 0;

	return 1;
}

static int check_host_rd(void)
{
	/* Make sure CC_RP3A0_RD_L is configured as GPO */
	if (mock_get_cfg_cc_rp3a0_rd_l() != PIN_GPO)
		return 0;

	/* Make sure CC_RP3A0_RD_L is asserted low */
	if (mock_get_cc_rp3a0_rd_l() != 0)
		return 0;

	/* Make sure VPDMCU_CC_EN is enabled */
	if (mock_get_mcu_cc_en() != 1)
		return 0;

	/* Make sure CC_VPDMCU is configured as ADC */
	if (mock_get_cfg_cc_vpdmcu() != PIN_ADC)
		return 0;

	/* Make sure CC_DB_EN_OD is LOW */
	if (mock_get_cc_db_en_od() != GPO_LOW)
		return 0;

	return 1;
}

#if defined(TEST_USB_TYPEC_CTVPD)
static int check_host_rp3a0(void)
{
	/* Make sure CC_RP3A0_RD_L is asserted high */
	if (mock_get_cc_rp3a0_rd_l() != 1)
		return 0;

	return 1;
}

static int check_host_rpusb(void)
{
	/* Make sure CC_RPUSB_ODH is asserted high */
	if (mock_get_cc_rpusb_odh() != 1)
		return 0;

	/* Make sure CC_RP3A0_RD_L is configured as comparator */
	if (mock_get_cfg_cc_rp3a0_rd_l() != PIN_CMP)
		return 0;

	return 1;
}

static int check_host_cc_open(void)
{
	/* Make sure CC_RPUSB_ODH is hi-z */
	if (mock_get_cc_rpusb_odh() != GPO_HZ)
		return 0;

	/* Make sure CC_RP3A0_RD_L is set to comparitor */
	if (mock_get_cfg_cc_rp3a0_rd_l() != PIN_CMP)
		return 0;

	/* Make sure cc_db_en_od is set low */
	if (mock_get_cc_db_en_od() != GPO_LOW)
		return 0;

	return 1;
}

static int check_ct_ccs_hz(void)
{
	return (mock_get_ct_rd() == GPO_HIGH);
}

static int check_ct_ccs_rd(void)
{
	return (mock_get_ct_rd() == GPO_LOW);
}

static int check_ct_ccs_cc1_rpusb(void)
{
	return (mock_get_ct_cc1_rpusb() == 1);
}
#endif

void inc_tx_id(int port)
{
	pd_port[port].msg_tx_id = (pd_port[port].msg_tx_id + 1) % 7;
}

void inc_rx_id(int port)
{
	pd_port[port].msg_rx_id = (pd_port[port].msg_rx_id + 1) % 7;
}

static int verify_goodcrc(int port, int role, int id)
{
	return pd_test_tx_msg_verify_sop_prime(port) &&
	       pd_test_tx_msg_verify_short(port,
					   PD_HEADER(PD_CTRL_GOOD_CRC, role,
						     role, id, 0, 0, 0)) &&
	       pd_test_tx_msg_verify_crc(port) &&
	       pd_test_tx_msg_verify_eop(port);
}

static void simulate_rx_msg(int port, uint16_t header, int cnt,
			    const uint32_t *data)
{
	int i;

	pd_test_rx_set_preamble(port, 1);
	pd_test_rx_msg_append_sop_prime(port);
	pd_test_rx_msg_append_short(port, header);

	crc32_init();
	crc32_hash16(header);

	for (i = 0; i < cnt; ++i) {
		pd_test_rx_msg_append_word(port, data[i]);
		crc32_hash32(data[i]);
	}

	pd_test_rx_msg_append_word(port, crc32_result());

	pd_test_rx_msg_append_eop(port);
	pd_test_rx_msg_append_last_edge(port);

	pd_simulate_rx(port);
}

static void simulate_goodcrc(int port, int role, int id)
{
	simulate_rx_msg(port,
			PD_HEADER(PD_CTRL_GOOD_CRC, role, role, id, 0,
				  pd_port[port].rev, 0),
			0, NULL);
}

static void simulate_discovery_identity(int port)
{
	uint16_t header = PD_HEADER(PD_DATA_VENDOR_DEF, PD_ROLE_SOURCE, 1,
				    pd_port[port].msg_rx_id, 1,
				    pd_port[port].rev, 0);
	uint32_t msg = VDO(USB_SID_PD, 1, /* Structured VDM */
			   VDO_SVDM_VERS_MAJOR(1) | VDO_CMDT(CMDT_INIT) |
				   CMD_DISCOVER_IDENT);

	simulate_rx_msg(port, header, 1, (const uint32_t *)&msg);
}

static int test_vpd_host_src_detection(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * TEST:
	 * Host is configured properly and start state is UNATTACHED_SNK
	 */
	TEST_ASSERT(check_host_ra_rd());
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	/*
	 * TEST:
	 * Host PORT Source Connection Detected
	 */

	host_connect_source(VBUS_0);
	mock_set_vconn(VCONN_0);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	/*
	 * TEST:
	 * Host CC debounce in ATTACH_WAIT_SNK state
	 */

	host_disconnect_source();

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(5 * MSEC);

	/*
	 * TEST:
	 * Host CC debounce in ATTACH_WAIT_SNK state
	 */

	host_connect_source(VBUS_0);
	mock_set_vconn(VCONN_0);

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(50 * MSEC);

	/*
	 * TEST:
	 * Host Port Connection Removed
	 */
	host_disconnect_source();

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	return EC_SUCCESS;
}

static int test_vpd_host_src_detection_vbus(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * TEST:
	 * Host is configured properly and start state is UNATTACHED_SNK
	 */

	TEST_ASSERT(check_host_ra_rd());
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	/*
	 * TEST:
	 * Host Port Source Connection Detected
	 */

	host_connect_source(VBUS_0);
	mock_set_vconn(VCONN_0);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	/*
	 * TEST:
	 * Host Port Source Detected for tCCDebounce and Host Port VBUS
	 * Detected.
	 */

	host_connect_source(VBUS_5);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);

	/*
	 * TEST:
	 * Host Port VBUS Removed
	 */

	host_connect_source(VBUS_0);

	/*
	 * The state changes from UNATTACHED_SNK to ATTACH_WAIT_SNK immediately
	 * if Rp is detected.
	 */
	wait_for_state_change(port, 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	return EC_SUCCESS;
}

static int test_vpd_host_src_detection_vconn(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * TEST:
	 * Host is configured properly and start state is UNATTACHED_SNK
	 */

	TEST_ASSERT(check_host_ra_rd());
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	/*
	 * TEST:
	 * Host Source Connection Detected
	 */

	host_connect_source(VBUS_0);
	mock_set_vconn(VCONN_0);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	/*
	 * TEST:
	 * Host Port Source Detected for tCCDebounce and VCONN Detected
	 */

	host_connect_source(VBUS_0);
	mock_set_vconn(VCONN_3);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);

	/* VCONN was detected. Make sure RA is removed */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);
	TEST_ASSERT(check_host_rd());

	/*
	 * TEST:
	 * Host Port VCONN Removed
	 */

	mock_set_host_cc_source_voltage(0);
	mock_set_vconn(VCONN_0);

	wait_for_state_change(port, 10 * MSEC);

	TEST_EQ(get_state_tc(port), TC_UNATTACHED_SNK, "%d");

	host_disconnect_source();

	return EC_SUCCESS;
}

static int test_vpd_host_src_detection_message_reception(void)
{
	int port = PORT0;
	uint32_t expected_vdm_header =
		VDO(USB_VID_GOOGLE, 1, /* Structured VDM */
		    VDO_SVDM_VERS_MAJOR(1) | VDO_CMDT(CMDT_RSP_ACK) |
			    CMD_DISCOVER_IDENT);
	uint32_t expected_vdo_id_header =
		VDO_IDH(0, /* Not a USB Host */
			1, /* Capable of being enumerated as USB Device */
			IDH_PTYPE_VPD, 0, /* Modal Operation Not Supported */
			USB_VID_GOOGLE);
	uint32_t expected_vdo_cert = 0;
	uint32_t expected_vdo_product =
		VDO_PRODUCT(CONFIG_USB_PID, USB_BCD_DEVICE);
	uint32_t expected_vdo_vpd = VDO_VPD(
		VPD_HW_VERSION, VPD_FW_VERSION, VPD_MAX_VBUS_20V,
		IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_CT_CURRENT : 0,
		IS_ENABLED(CONFIG_USB_CTVPD) ?
			VPD_VBUS_IMP(VPD_VBUS_IMPEDANCE) :
			0,
		IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_GND_IMP(VPD_GND_IMPEDANCE) :
					       0,
		IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_CTS_SUPPORTED :
					       VPD_CTS_NOT_SUPPORTED);

	mock_set_vconn(VCONN_0);
	host_disconnect_source();

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * TEST:
	 * Host is configured properly and start state is UNATTACHED_SNK
	 */

	TEST_ASSERT(check_host_ra_rd());
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	/*
	 * Transition to ATTACHED_SNK
	 */

	host_connect_source(VBUS_5);

	wait_for_state_change(port, 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 20 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);

	/* Run state machines to enable rx monitoring */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/*
	 * TEST:
	 * Reception of Discovery Identity message
	 */

	simulate_discovery_identity(port);
	task_wait_event(30 * MSEC);

	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_rx_id(port);

	/* Test Discover Identity Ack */
	TEST_ASSERT(pd_test_tx_msg_verify_sop_prime(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_VENDOR_DEF, PD_PLUG_FROM_CABLE, 0,
			  pd_port[port].msg_tx_id, 5, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdm_header));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_id_header));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_cert));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_product));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_vpd));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	/* Ack was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);
	inc_tx_id(port);

	/*
	 * TEST:
	 * Host Port VBUS Removed
	 */

	host_disconnect_source();

	wait_for_state_change(port, 100 * MSEC);

	TEST_EQ(get_state_tc(port), TC_UNATTACHED_SNK, "%d");

	return EC_SUCCESS;
}

#if defined(TEST_USB_TYPEC_CTVPD)
static int test_ctvpd_behavior_case1(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();
	TEST_ASSERT(ct_disconnect_source());
	TEST_ASSERT(ct_disconnect_sink());

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * CASE 1: The following tests the behavior when a DRP is connected to a
	 *	 Charge-Through VCONN-Powered USB Device (abbreviated CTVPD),
	 *	 with no Power Source attached to the ChargeThrough port on
	 *	 the CTVPD.
	 */

	/* 1. DRP and CTVPD are both in the unattached state */
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	/*
	 *   a. DRP alternates between Unattached.SRC and Unattached.SNK
	 *
	 *   b. CTVPD has applied Rd on its Charge-Through port’s CC1 and CC2
	 *   pins and Rd on the Host-side port’s CC pin
	 */
	TEST_ASSERT(check_host_ra_rd());
	TEST_ASSERT(check_ct_ccs_rd());

	/*
	 * 2. DRP transitions from Unattached.SRC to AttachWait.SRC to
	 *    Attached.SRC
	 *
	 *    a. DRP in Unattached.SRC detects the CC pull-down of CTVPD which
	 *       is in Unattached.SNK and DRP enters AttachWait.SRC
	 *    b. DRP in AttachWait.SRC detects that pull down on CC persists for
	 *       tCCDebounce, enters Attached.SRC and turns on VBUS and VCONN
	 */
	host_connect_source(VBUS_5);
	mock_set_vconn(VCONN_3);

	/*
	 * 3. CTVPD transitions from Unattached.SNK to Attached.SNK through
	 *    AttachWait.SNK.
	 *
	 *    a. CTVPD detects the host-side CC pull-up of the DRP and CTVPD
	 *       enters AttachWait.SNK
	 *    b. CTVPD in AttachWait.SNK detects that pull up on the Host-side
	 *       port’s CC persists for tCCDebounce, VCONN present and enters
	 *       Attached.SNK
	 *    c. CTVPD present a high-impedance to ground (above zOPEN) on its
	 *       Charge-Through port’s CC1 and CC2 pins
	 */
	wait_for_state_change(port, 40 * MSEC);
	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);
	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);
	TEST_ASSERT(check_ct_ccs_hz());

	/*
	 * 4. While DRP and CTVPD are in their respective attached states, DRP
	 *    discovers the ChargeThrough CTVPD and transitions to
	 *    CTUnattached.SNK
	 *
	 *    a. DRP (as Source) queries the device identity via USB PD
	 *       (Device Identity Command) on SOP’.
	 *    b. CTVPD responds on SOP’, advertising that it is a
	 *       Charge-Through VCONN-Powered USB Device
	 *    c. DRP (as Source) removes VBUS
	 *    d. DRP (as Source) changes its Rp to a Rd
	 *    e. DRP (as Sink) continues to provide VCONN and enters
	 *       CTUnattached.SNK
	 */
	host_disconnect_source();

	/*
	 * 5. CTVPD transitions to CTUnattached.VPD
	 *
	 *    a. CTVPD detects VBUS removal, VCONN presence, the low Host-side
	 *       CC pin and enters CTUnattached.VPD
	 *    b. CTVPD changes its host-side Rd to a Rp advertising 3.0 A
	 *    c. CTVPD isolates itself from VBUS
	 *    d. CTVPD apply Rd on its Charge-Through port’s CC1 and CC2 pins
	 */
	wait_for_state_change(port, 40 * MSEC);
	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);

	/*
	 * 6. While the CTVPD in CTUnattached.VPD state and the DRP in
	 *    CTUnattached.SNK state:
	 *
	 *    a. CTVPD monitors Charge-Though CC pins for a source or sink;
	 *       when a Power Source attach is detected, enters
	 *       CTAttachWait.VPD; when a sink is detected, enters
	 *       CTAttachWait.Unsupported
	 *    b. CTVPD monitors VCONN for Host detach and when detected, enters
	 *       Unattached.SNK
	 *    c. DRP monitors VBUS and CC for CTVPD detach for tVPDDetach and
	 *       when detected, enters Unattached.SNK
	 *    d. DRP monitors VBUS for Power Source attach and when detected,
	 *       enters CTAttached.SNK
	 */
	/* Attach Power Source */
	TEST_ASSERT(ct_connect_source(CC2, VBUS_0));

	wait_for_state_change(port, 40 * MSEC);
	TEST_EQ(get_state_tc(port), TC_CT_ATTACH_WAIT_VPD, "%d");

	/* Remove Power Source */
	TEST_ASSERT(ct_disconnect_source());

	wait_for_state_change(port, 40 * MSEC);

	TEST_EQ(get_state_tc(port), TC_CT_UNATTACHED_VPD, "%d");

	/* Attach Sink */
	TEST_ASSERT(ct_connect_sink(CC1, SRC_CON_DEF));

	wait_for_state_change(port, 40 * MSEC);

	TEST_EQ(get_state_tc(port), TC_CT_ATTACH_WAIT_UNSUPPORTED, "%d");

	/* Remove VCONN (Host detach) */
	mock_set_vconn(VCONN_0);

	wait_for_state_change(port, 40 * MSEC);

	TEST_EQ(get_state_tc(port), TC_UNATTACHED_SNK, "%d");

	return EC_SUCCESS;
}

static int test_ctvpd_behavior_case2(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();
	TEST_ASSERT(ct_disconnect_source());
	TEST_ASSERT(ct_disconnect_sink());

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * CASE 2: The following tests the behavior when a Power Source is
	 *	 connected to a Charge-Through VCONN-Powered USB Device
	 *	 (abbreviated CTVPD), with a Host already attached to the
	 *	 Host-Side port on the CTVPD.
	 */

	/*
	 * 1. DRP is in CTUnattached.SNK state, CTVPD in CTUnattached.VPD, and
	 *    Power Source in the unattached state
	 *
	 *    a. CTVPD has applied Rd on the Charge-Through port’s CC1 and CC2
	 *       pins and Rp termination advertising 3.0 A on the Host-side
	 *       port’s CC pin
	 */
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	host_connect_source(VBUS_5);
	mock_set_vconn(VCONN_3);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);

	/* Remove Host CC */
	mock_set_host_cc_source_voltage(0);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);
	TEST_ASSERT(check_ct_ccs_rd());
	TEST_ASSERT(check_host_rp3a0());

	/*
	 * 2. Power Source transitions from Unattached.SRC to Attached.SRC
	 *    through AttachWait.SRC.
	 *
	 *    a. Power Source detects the CC pull-down of the CTVPD and enters
	 *       AttachWait.SRC
	 *    b. Power Source in AttachWait.SRC detects that pull down on CC
	 *       persists for tCCDebounce, enters Attached.SRC and turns on
	 *       VBUS
	 */
	TEST_ASSERT(ct_connect_source(CC2, VBUS_5));

	/*
	 * 3. CTVPD transitions from CTUnattached.VPD through CTAttachWait.VPD
	 *    to CTAttached.VPD
	 *
	 *    a. CTVPD detects the Source’s Rp on one of its Charge-Through CC
	 *       pins, and transitions to CTAttachWait.VPD
	 *    b. CTVPD finishes any active USB PD communication on SOP’ and
	 *       ceases to respond to SOP’ queries
	 *    c. CTVPD in CTAttachWait.VPD detects that the pull up on
	 *       Charge-Through CC pin persists for tCCDebounce, detects VBUS
	 *       and enters CTAttached.VPD
	 *    d. CTVPD connects the active Charge-Through CC pin to the
	 *       Host-side port’s CC pin
	 *    e. CTVPD disables its Rp termination advertising 3.0 A on the
	 *       Host-side port’s CC pin
	 *    f. CTVPD disables its Rd on the Charge-Through CC pins
	 *    g. CTVPD connects VBUS from the Charge-Through side to the Host
	 *       side
	 */

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_ATTACH_WAIT_VPD);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_ATTACHED_VPD);
	TEST_ASSERT(moch_get_ct_cl_sel() == CT_CC2);
	TEST_ASSERT(check_host_cc_open());
	TEST_ASSERT(check_ct_ccs_hz());
	TEST_ASSERT(mock_get_vbus_pass_en());

	/*
	 * 4. DRP (as Sink) transitions to CTAttached.SNK
	 *    a. DRP (as Sink) detects VBUS, monitors vRd for available current
	 *       and enter CTAttached.SNK
	 */

	/*
	 * 5. While the devices are all in their respective attached states:
	 *    a. CTVPD monitors VCONN for DRP detach and when detected,
	 *       enters CTDisabled.VPD
	 *    b. CTVPD monitors VBUS and CC for Power Source detach and when
	 *       detected, enters CTUnattached.VPD within tVPDCTDD
	 *    c. DRP (as Sink) monitors VBUS for Charge-Through Power Source
	 *       detach and when detected, enters CTUnattached.SNK
	 *    d. DRP (as Sink) monitors VBUS and CC for CTVPD detach and when
	 *       detected, enters Unattached.SNK (and resumes toggling between
	 *       Unattached.SNK and Unattached.SRC)
	 *    e. Power Source monitors CC for CTVPD detach and when detected,
	 *       enters Unattached.SRC
	 */
	mock_set_vconn(VCONN_0);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_DISABLED_VPD);

	return EC_SUCCESS;
}

static int test_ctvpd_behavior_case3(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();
	TEST_ASSERT(ct_disconnect_source());
	TEST_ASSERT(ct_disconnect_sink());

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * CASE 3: The following describes the behavior when a Power Source is
	 *	 connected to a ChargeThrough VCONN-Powered USB Device
	 *	 (abbreviated CTVPD), with no Host attached to the Host-side
	 *	 port on the CTVPD.
	 */

	/*
	 * 1. CTVPD and Power Source are both in the unattached state
	 *    a. CTVPD has applied Rd on the Charge-Through port’s CC1 and CC2
	 *       pins and Rd on the Host-side port’s CC pin
	 */
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	TEST_ASSERT(check_ct_ccs_rd());
	TEST_ASSERT(check_host_ra_rd());
	TEST_ASSERT(ct_connect_source(CC2, VBUS_5));

	/*
	 * 2. Power Source transitions from Unattached.SRC to Attached.SRC
	 *    through AttachWait.SRC.
	 *
	 *    a. Power Source detects the CC pull-down of the CTVPD and enters
	 *       AttachWait.SRC
	 *    b. Power Source in AttachWait.SRC detects that pull down on CC
	 *       persists for tCCDebounce, enters Attached.SRC and turns on
	 *       VBUS
	 */

	/* 3. CTVPD alternates between Unattached.SNk and Unattached.SRC
	 *
	 *    a. CTVPD detects the Source’s Rp on one of its Charge-Through CC
	 *       pins, detects VBUS for tCCDebounce and starts alternating
	 *       between Unattached.SRC and Unattached.SNK
	 */
	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SRC);

	/*
	 * 4. While the CTVPD alternates between Unattached.SRC and
	 *    Unattached.SNK state and the Power Source in Attached.SRC state:
	 *
	 *    a. CTVPD monitors the Host-side port’s CC pin for device attach
	 *       and when detected, enters AttachWait.SRC
	 *    b. CTVPD monitors VBUS for Power Source detach and when detected,
	 *       enters Unattached.SNK
	 *    c. Power Source monitors CC for CTVPD detach and when detected,
	 *       enters Unattached.SRC
	 */

	/* Attached host side device */
	host_connect_sink(SRC_CON_DEF);

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SRC);

	/* Remove VBUS */
	TEST_ASSERT(ct_disconnect_source());

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	return EC_SUCCESS;
}

static int test_ctvpd_behavior_case4(void)
{
	int port = PORT0;
	uint32_t expected_vdm_header =
		VDO(USB_VID_GOOGLE, 1, /* Structured VDM */
		    VDO_SVDM_VERS_MAJOR(1) | VDO_CMDT(CMDT_RSP_ACK) |
			    CMD_DISCOVER_IDENT);
	uint32_t expected_vdo_id_header =
		VDO_IDH(0, /* Not a USB Host */
			1, /* Capable of being enumerated as USB Device */
			IDH_PTYPE_VPD, 0, /* Modal Operation Not Supported */
			USB_VID_GOOGLE);
	uint32_t expected_vdo_cert = 0;
	uint32_t expected_vdo_product =
		VDO_PRODUCT(CONFIG_USB_PID, USB_BCD_DEVICE);
	uint32_t expected_vdo_vpd = VDO_VPD(
		VPD_HW_VERSION, VPD_FW_VERSION, VPD_MAX_VBUS_20V,
		IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_CT_CURRENT : 0,
		IS_ENABLED(CONFIG_USB_CTVPD) ?
			VPD_VBUS_IMP(VPD_VBUS_IMPEDANCE) :
			0,
		IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_GND_IMP(VPD_GND_IMPEDANCE) :
					       0,
		IS_ENABLED(CONFIG_USB_CTVPD) ? VPD_CTS_SUPPORTED :
					       VPD_CTS_NOT_SUPPORTED);

	init_port(port);
	mock_set_vconn(VCONN_0);
	host_disconnect_source();
	TEST_ASSERT(ct_disconnect_source());
	TEST_ASSERT(ct_disconnect_sink());

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * CASE 4: The following describes the behavior when a DRP is connected
	 *	 to a Charge-Through VCONN-Powered USB Device
	 *	 (abbreviated CTVPD), with a Power Source already attached to
	 *	 the Charge-Through side on the CTVPD.
	 */

	/*
	 * 1. DRP, CTVPD and Sink are all in the unattached state
	 *
	 *    a. DRP alternates between Unattached.SRC and Unattached.SNK
	 *    b. CTVPD has applied Rd on its Charge-Through port’s CC1 and CC2
	 *       pins and Rd on the Host-side port’s CC pin
	 */
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	TEST_ASSERT(check_ct_ccs_rd());
	TEST_ASSERT(check_host_ra_rd());

	/*
	 * 2. DRP transitions from Unattached.SRC to AttachWait.SRC to
	 *    Attached.SRC
	 *
	 *    a. DRP in Unattached.SRC detects the CC pull-down of CTVPD which
	 *       is in Unattached.SNK and DRP enters AttachWait.SRC
	 *    b. DRP in AttachWait.SRC detects that pull down on CC persists
	 *       for tCCDebounce, enters Attached.SRC and turns on VBUS and
	 *       VCONN
	 */

	host_connect_source(VBUS_5);
	mock_set_vconn(VCONN_3);

	/*
	 * 3. CTVPD transitions from Unattached.SNK to Attached.SNK through
	 *    AttachWait.SNK.
	 *
	 *    a. CTVPD detects the host-side CC pull-up of the DRP and CTVPD
	 *       enters AttachWait.SNK
	 *    b. CTVPD in AttachWait.SNK detects that pull up on the
	 *       Host-side port’s CC persists for tCCDebounce, VCONN present
	 *       and enters Attached.SNK
	 *    c. CTVPD present a high-impedance to ground (above zOPEN) on its
	 *       Charge-Through port’s CC1 and CC2 pins
	 */

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);
	TEST_ASSERT(check_ct_ccs_hz());

	/*
	 * 4. While DRP and CTVPD are in their respective attached states, DRP
	 *    discovers the ChargeThrough CTVPD and transitions to
	 *    CTUnattached.SNK
	 *
	 *    a. DRP (as Source) queries the device identity via USB PD
	 *       (Discover Identity Command) on SOP’.
	 *    b. CTVPD responds on SOP’, advertising that it is a
	 *       Charge-Through VCONN-Powered USB Device
	 *    c. DRP (as Source) removes VBUS
	 *    d. DRP (as Source) changes its Rp to a Rd
	 *    e. DRP (as Sink) continues to provide VCONN and enters
	 *       CTUnattached.SNK
	 */

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	simulate_discovery_identity(port);
	task_wait_event(40 * MSEC);

	TEST_ASSERT(
		verify_goodcrc(port, PD_ROLE_SINK, pd_port[port].msg_rx_id));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);
	inc_rx_id(port);

	/* Test Discover Identity Ack */
	TEST_ASSERT(pd_test_tx_msg_verify_sop_prime(port));
	TEST_ASSERT(pd_test_tx_msg_verify_short(
		port,
		PD_HEADER(PD_DATA_VENDOR_DEF, PD_PLUG_FROM_CABLE, 0,
			  pd_port[port].msg_tx_id, 5, pd_port[port].rev, 0)));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdm_header));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_id_header));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_cert));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_product));
	TEST_ASSERT(pd_test_tx_msg_verify_word(port, expected_vdo_vpd));
	TEST_ASSERT(pd_test_tx_msg_verify_crc(port));
	TEST_ASSERT(pd_test_tx_msg_verify_eop(port));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/* Ack was good. Send GoodCRC */
	simulate_goodcrc(port, PD_ROLE_SOURCE, pd_port[port].msg_tx_id);
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);
	inc_tx_id(port);

	/*
	 * 5. CTVPD transitions to CTUnattached.VPD
	 *
	 *    a. CTVPD detects VBUS removal, VCONN presence, the low Host-side
	 *       CC pin and enters CTUnattached.VPD
	 *    b. CTVPD changes its host-side Rd to a Rp termination advertising
	 *       3.0 A
	 *    c. CTVPD isolates itself from VBUS
	 *    d. CTVPD apply Rd on its Charge-Through port’s CC1 and CC2 pins
	 */
	host_disconnect_source();

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);
	TEST_ASSERT(check_ct_ccs_rd());
	TEST_ASSERT(check_host_rp3a0());

	/*
	 * 6. CTVPD alternates between CTUnattached.VPD and
	 *    CTUnattached.Unsupported
	 */
	wait_for_state_change(port, PD_T_DRP_SRC + 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_UNSUPPORTED);

	wait_for_state_change(port, PD_T_DRP_SRC + 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);
	TEST_ASSERT(ct_connect_source(CC2, VBUS_5));

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_ATTACH_WAIT_VPD);

	return EC_SUCCESS;
}

static int test_ctvpd_behavior_case5(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();
	TEST_ASSERT(ct_disconnect_source());
	TEST_ASSERT(ct_disconnect_sink());

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * CASE 5: The following describes the behavior when a Power Source is
	 *	 connected to a ChargeThrough VCONN-Powered USB Device
	 *	 (abbreviated CTVPD), with a DRP (with dead battery) attached
	 *	 to the Host-side port on the CTVPD.
	 */

	/*
	 * 1. DRP, CTVPD and Power Source are all in the unattached state
	 *
	 *    a. DRP apply dead battery Rd
	 *    b. CTVPD apply Rd on the Charge-Through port’s CC1 and CC2 pins
	 *       and Rd on the Host-side port’s CC pin
	 */
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	TEST_ASSERT(check_ct_ccs_rd());
	TEST_ASSERT(check_host_ra_rd());

	/*
	 * 2. Power Source transitions from Unattached.SRC to Attached.SRC
	 *    through AttachWait.SRC.
	 *
	 *    a. Power Source detects the CC pull-down of the CTVPD and enters
	 *       AttachWait.SRC
	 *    b. Power Source in AttachWait.SRC detects that pull down on CC
	 *       persists for tCCDebounce, enters Attached.SRC and enable VBUS
	 */
	TEST_ASSERT(ct_connect_source(CC2, VBUS_5));

	/*
	 * 3. CTVPD alternates between Unattached.SNK and Unattached.SRC
	 *
	 *    a. CTVPD detects the Source’s Rp on one of its Charge-Through CC
	 *       pins, detects VBUS for tCCDebounce and starts alternating
	 *       between Unattached.SRC and Unattached.SNK
	 */

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SRC);

	/* Connect Host With Dead Battery */
	host_connect_sink(SRC_CON_DEF);

	/*
	 * 4. CTVPD transitions from Unattached.SRC to Try.SNK through
	 *    AttachWait.SRC
	 *
	 *    a. CTVPD in Unattached.SRC detects the CC pull-down of DRP which
	 *       is in Unattached.SNK and CTVPD enters AttachWait.SRC
	 *    b. CTVPD in AttachWait.SRC detects that pull down on CC persists
	 *       for tCCDebounce and enters Try.SNK
	 *    c. CTVPD disables Rp termination advertising Default USB Power on
	 *       the Host-side port’s CC
	 *    d. CTVPD enables Rd on the Host-side port’s CC
	 */

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SRC);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 10 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_TRY_SNK);
	TEST_ASSERT(check_host_ra_rd());

	/* 5. DRP in dead battery condition remains in Unattached.SNK */

	/*
	 * 6. CTVPD transitions from Try.SNK to Attached.SRC through
	 *    TryWait.SRC
	 *
	 *    a. CTVPD didn’t detect the CC pull-up of the DRP for
	 *       tTryDebounce after tDRPTry and enters TryWait.SRC
	 *    b. CTVPD disables Rd on the Host-side port’s CC
	 *    c. CTVPD enables Rp termination advertising Default USB Power on
	 *       the Host-side port’s CC
	 *    d. CTVPD detects the CC pull-down of the DRP for tTryCCDebounce
	 *       and enters Attached.SRC
	 *    e. CTVPD connects VBUS from the Charge-Through side to the Host
	 *       side
	 */
	wait_for_state_change(port,
			      PD_T_TRY_CC_DEBOUNCE + PD_T_DRP_TRY + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_TRY_WAIT_SRC);
	TEST_ASSERT(check_host_rpusb());

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SRC);
	TEST_ASSERT(mock_get_vbus_pass_en());

	/*
	 * 7. DRP transitions from Unattached.SNK to Attached.SNK through
	 *    AttachWait.SNK
	 *
	 *    a. DRP in Unattached.SNK detects the CC pull-up of CTVPD which is
	 *       in Attached.SRC and DRP enters AttachWait.SNK
	 *    b. DRP in AttachWait.SNK detects that pull up on CC persists for
	 *       tCCDebounce, VBUS present and enters Attached.SNK
	 */

	/*
	 * 8. While the devices are all in their respective attached states:
	 *    a. CTVPD monitors the Host-side port’s CC pin for device attach
	 *       and when detected, enters Unattached.SNK
	 *    b. CTVPD monitors VBUS for Power Source detach and when detected,
	 *       enters Unattached.SNK
	 *    c. Power Source monitors CC for CTVPD detach and when detected,
	 *       enters Unattached.SRC
	 *    d. DRP monitors VBUS for CTVPD detach and when detected, enters
	 *       Unattached.SNK
	 *    e. Additionally, the DRP may query the identity of the cable via
	 *       USB PD on SOP’ when it has sufficient battery power and when
	 *       a Charge-Through VPD is identified enters TryWait.SRC if
	 *       implemented, or enters Unattached.SRC if TryWait.SRC is not
	 *       supported
	 */
	TEST_ASSERT(ct_connect_source(CC2, VBUS_0));

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);

	return EC_SUCCESS;
}

static int test_ctvpd_behavior_case6(void)
{
	int port = PORT0;

	mock_set_vconn(VCONN_0);
	host_disconnect_source();
	TEST_ASSERT(ct_disconnect_source());
	TEST_ASSERT(ct_disconnect_sink());

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	/*
	 * CASE 6: The following describes the behavior when a DRP is connected
	 *	 to a Charge-Through VCONN-Powered USB Device
	 *	 (abbreviated CTVPD) and a Sink is attached to the
	 *	 Charge-Through port on the CTVPD.
	 */

	/*
	 * 1. DRP, CTVPD and Sink are all in the unattached state
	 *
	 *    a. DRP alternates between Unattached.SRC and Unattached.SNK
	 *    b. CTVPD has applied Rd on its Charge-Through port’s CC1 and CC2
	 *       pins and Rd on the Host-side port’s CC pin
	 */
	TEST_ASSERT(get_state_tc(port) == TC_UNATTACHED_SNK);
	TEST_ASSERT(check_ct_ccs_rd());
	TEST_ASSERT(check_host_ra_rd());

	/*
	 * 2. DRP transitions from Unattached.SRC to AttachWait.SRC to
	 *    Attached.SRC
	 *
	 *    a. DRP in Unattached.SRC detects the CC pull-down of CTVPD which
	 *       is in Unattached.SNK and DRP enters AttachWait.SRC
	 *    b. DRP in AttachWait.SRC detects that pull down on CC persists
	 *       for tCCDebounce, enters Attached.SRC and turns on VBUS and
	 *       VCONN
	 */
	host_connect_source(VBUS_5);
	mock_set_vconn(VCONN_3);

	/*
	 * 3. CTVPD transitions from Unattached.SNK to Attached.SNK through
	 *    AttachWait.SNK.
	 *
	 *    a. CTVPD detects the host-side CC pull-up of the DRP and CTVPD
	 *       enters AttachWait.SNK
	 *    b. CTVPD in AttachWait.SNK detects that pull up on the Host-side
	 *       port’s CC persists for tCCDebounce, VCONN present and enters
	 *       Attached.SNK
	 *    c. CTVPD present a high-impedance to ground (above zOPEN) on its
	 *       Charge-Through port’s CC1 and CC2 pins
	 */
	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACH_WAIT_SNK);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_ATTACHED_SNK);
	TEST_ASSERT(check_ct_ccs_hz());

	/*
	 * 4. While DRP and CTVPD are in their respective attached states, DRP
	 *    discovers the ChargeThrough CTVPD and transitions to
	 *    CTUnattached.SNK
	 *
	 *    a. DRP (as Source) queries the device identity via USB PD
	 *       (Discover Identity Command) on SOP’.
	 *    b. CTVPD responds on SOP’, advertising that it is a
	 *       Charge-Through VCONN-Powered USB Device
	 *    c. DRP (as Source) removes VBUS
	 *    d. DRP (as Source) changes its Rp to a Rd
	 *    e. DRP (as Sink) continues to provide VCONN and enters
	 *       CTUnattached.SNK
	 */

	host_disconnect_source();
	host_connect_sink(SRC_CON_DEF);

	/*
	 * 5. CTVPD transitions to CTUnattached.VPD
	 *
	 *    a. CTVPD detects VBUS removal, VCONN presence, the low Host-side
	 *       CC pin and enters CTUnattached.VPD
	 *    b. CTVPD changes its host-side Rd to a Rp termination advertising
	 *       3.0 A
	 *    c. CTVPD isolates itself from VBUS
	 *    d. CTVPD apply Rd on its Charge-Through port’s CC1 and CC2 pins
	 */
	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);
	TEST_ASSERT(check_host_rp3a0());
	TEST_ASSERT(mock_get_vbus_pass_en() == 0);
	TEST_ASSERT(check_ct_ccs_rd());

	/*
	 * 6. CTVPD alternates between CTUnattached.VPD and
	 *    CTUnattached.Unsupported
	 *
	 *    a. CTVPD detects SRC.open on its Charge-Through CC pins and
	 *       starts alternating between CTUnattached.VPD and
	 *       CTUnattached.Unsupported
	 */
	wait_for_state_change(port, PD_T_DRP_SNK + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_UNSUPPORTED);

	wait_for_state_change(port, PD_T_DRP_SNK + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);

	wait_for_state_change(port, PD_T_DRP_SNK + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_UNSUPPORTED);

	/*
	 * 7. CTVPD transitions from CTUnattached.Unsupported to CTTry.SNK
	 *    through CTAttachWait.Unsupported
	 *
	 *    a. CTVPD in CTUnattached.Unsupported detects the CC pull-down of
	 *       the Sink which is in Unattached.SNK and CTVPD enters
	 *       CTAttachWait.Unsupported
	 *    b. CTVPD in CTAttachWait.Unsupported detects that pull down on CC
	 *       persists for tCCDebounce and enters CTTry.SNK
	 *    c. CTVPD disables Rp termination advertising Default USB Power on
	 *       the ChargeThrough port’s CC pins
	 *    d. CTVPD enables Rd on the Charge-Through port’s CC pins
	 */
	TEST_ASSERT(ct_connect_sink(CC1, SRC_CON_DEF));

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_ATTACH_WAIT_UNSUPPORTED);

	wait_for_state_change(port, PD_T_CC_DEBOUNCE + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_TRY_SNK);
	TEST_ASSERT(check_ct_ccs_rd());

	/*
	 * 8. CTVPD transitions from CTTry.SNK to CTAttached.Unsupported
	 *
	 *    a. CTVPD didn’t detect the CC pull-up of the potential Source
	 *       for tDRPTryWait after tDRPTry and enters
	 *       CTAttached.Unsupported
	 */

	wait_for_state_change(port, PD_T_DRP_TRY + PD_T_TRY_WAIT + 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_ATTACHED_UNSUPPORTED);

	/*
	 * 9. While the CTVPD in CTAttached.Unsupported state, the DRP in
	 *    CTUnattached.SNK state and the Sink in Unattached.SNK state:
	 *
	 *    a. CTVPD disables the Rd termination on the Charge-Through
	 *       port’s CC pins and applies Rp termination advertising
	 *       Default USB Power
	 *    b. CTVPD exposes a USB Billboard Device Class to the DRP
	 *       indicating that it is connected to an unsupported device on
	 *       its Charge Through port
	 *    c. CTVPD monitors Charge-Though CC pins for Sink detach and when
	 *       detected, enters CTUnattached.VPD
	 *    d. CTVPD monitors VCONN for Host detach and when detected, enters
	 *       Unattached.SNK
	 *    e. DRP monitors CC for CTVPD detach for tVPDDetach and when
	 *       detected, enters Unattached.SNK
	 *    f. DRP monitors VBUS for CTVPD Charge-Through source attach and,
	 *       when detected, enters CTAttached.SNK
	 */

	TEST_ASSERT(check_ct_ccs_cc1_rpusb());
	TEST_ASSERT(mock_get_present_billboard() == BB_SNK);

	TEST_ASSERT(ct_disconnect_sink());

	wait_for_state_change(port, 40 * MSEC);

	TEST_ASSERT(get_state_tc(port) == TC_CT_UNATTACHED_VPD);

	return EC_SUCCESS;
}
#endif

void run_test(int argc, const char **argv)
{
	test_reset();

	init_port(PORT0);

	/* VPD and CTVPD tests */
	RUN_TEST(test_vpd_host_src_detection);
	RUN_TEST(test_vpd_host_src_detection_vbus);
	RUN_TEST(test_vpd_host_src_detection_vconn);
	RUN_TEST(test_vpd_host_src_detection_message_reception);

	/* CTVPD only tests */
#if defined(TEST_USB_TYPEC_CTVPD)
	/* DRP to VCONN-Powered USB Device (CTVPD) Behavior Tests */
	RUN_TEST(test_ctvpd_behavior_case1);
	RUN_TEST(test_ctvpd_behavior_case2);
	RUN_TEST(test_ctvpd_behavior_case3);
	RUN_TEST(test_ctvpd_behavior_case4);
	RUN_TEST(test_ctvpd_behavior_case5);
	RUN_TEST(test_ctvpd_behavior_case6);
#endif

	/* Do basic state machine validity checks last. */
	RUN_TEST(test_tc_no_parent_cycles);
	RUN_TEST(test_tc_all_states_named);

	/*
	 * Since you have to include TypeC layer when adding PE layer, the
	 * PE test would have the same build dependencies, so go ahead and test
	 * te PE statemachine here so we don't have to create another test exe
	 */
	RUN_TEST(test_pe_no_parent_cycles);
	RUN_TEST(test_pe_all_states_named);

	/* Some handlers are still running after the test ends. */
	crec_sleep(1);

	test_print_result();
}
