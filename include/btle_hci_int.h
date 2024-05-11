/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Copied from NewBlue hci_int.h with permission from Dmitry Grinberg, the
 * original author.
 */

#ifndef _HCI_INT_H_
#define _HCI_INT_H_

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HCI_DEV_NAME_LEN 248

#define HCI_INQUIRY_LENGTH_UNIT 1280 /* msec */
#define HCI_INQUIRY_LENGTH_MAX 48 /* units */

#define HCI_LAP_Unlimited_Inquiry 0x9E8B33
#define HCI_LAP_Limited_Inquiry 0x9E8B00

#define HCI_CLOCK_OFST_VALID 0x8000

#define HCI_PKT_TYP_NO_2_DH1 0x0002 /* BT 2.1+ */
#define HCI_PKT_TYP_NO_3_DH1 0x0004 /* BT 2.1+ */
#define HCI_PKT_TYP_DM1 0x0008 /* BT 1.1+ */
#define HCI_PKT_TYP_DH1 0x0010 /* BT 1.1+ */
#define HCI_PKT_TYP_NO_2_DH3 0x0100 /* BT 2.1+ */
#define HCI_PKT_TYP_NO_3_DH3 0x0200 /* BT 2.1+ */
#define HCI_PKT_TYP_DM3 0x0400 /* BT 1.1+ */
#define HCI_PKT_TYP_DH3 0x0800 /* BT 1.1+ */
#define HCI_PKT_TYP_NO_2_DH5 0x1000 /* BT 2.1+ */
#define HCI_PKT_TYP_NO_3_DH5 0x1000 /* BT 2.1+ */
#define HCI_PKT_TYP_DM5 0x4000 /* BT 1.1+ */
#define HCI_PKT_TYP_DH5 0x8000 /* BT 1.1+ */
#define HCI_PKT_TYP_DEFAULT 0xCC18

#define HCI_PKT_TYP_SCO_HV1 0x0001 /* BT 1.1+ */
#define HCI_PKT_TYP_SCO_HV2 0x0002 /* BT 1.1+ */
#define HCI_PKT_TYP_SCO_HV3 0x0004 /* BT 1.1+ */
#define HCI_PKT_TYP_SCO_EV3 0x0008 /* BT 1.2+ */
#define HCI_PKT_TYP_SCO_EV4 0x0010 /* BT 1.2+ */
#define HCI_PKT_TYP_SCO_EV5 0x0020 /* BT 1.2+ */
#define HCI_PKT_TYP_SCO_NO_2_EV3 0x0040 /* BT 2.1+ */
#define HCI_PKT_TYP_SCO_NO_3_EV3 0x0080 /* BT 2.1+ */
#define HCI_PKT_TYP_SCO_NO_2_EV5 0x0100 /* BT 2.1+ */
#define HCI_PKT_TYP_SCO_NO_3_EV5 0x0200 /* BT 2.1+ */

#define HCI_LINK_POLICY_DISABLE_ALL_LM_MODES 0x0000
#define HCI_LINK_POLICY_ENABLE_ROLESWITCH 0x0001
#define HCI_LINK_POLICY_ENABLE_HOLD_MODE 0x0002
#define HCI_LINK_POLICY_ENABLE_SNIFF_MODE 0x0004
#define HCI_LINK_POLICY_ENABLE_PARK_MODE 0x0008

#define HCI_FILTER_TYPE_CLEAR_ALL 0x00 /* no subtypes, no data */
#define HCI_FILTER_INQUIRY_RESULT 0x01 /* below subtypes */
#define HCI_FILTER_COND_TYPE_RETURN_ALL_DEVS 0x00 /* no data */
#define HCI_FILTER_COND_TYPE_SPECIFIC_DEV_CLS                                  \
	0x01 /* uint24_t wanted_class, uint24_t wanted_mask (only set bits are \
		compared to wanted_class) */
#define HCI_FILTER_COND_TYPE_SPECIFIC_ADDR 0x02 /* uint8_t mac[6] */
#define HCI_FILTER_CONNECTION_SETUP 0x02 /* below subtypes */
#define HCI_FILTER_COND_TYPE_ALLOW_CONNS_FROM_ALL_DEVS                        \
	0x00 /* uint8_t auto_accept_type: 1 - no, 2 - yes w/ no roleswitch, 3 \
		- yes w/ roleswitch */
#define HCI_FILTER_COND_TYPE_ALLOW_CONNS_FROM_SPECIFIC_DEV_CLS                 \
	0x01 /* uint24_t wanted_class, uint24_t wanted_mask (only set bits are \
		compared to wanted_class), auto_accept flag same as above */
#define HCI_FILTER_COND_TYPE_ALLOW_CONNS_FROM_SPECIFIC_ADDR \
	0x02 /* uint8_t mac[6], auto_accept flag same as above */

#define HCI_SCAN_ENABLE_INQUIRY 0x01 /* discoverable */
#define HCI_SCAN_ENABLE_PAGE 0x02 /* connectable */

#define HCI_HOLD_MODE_SUSPEND_PAGE_SCAN 0x01
#define HCI_HOLD_MODE_SUSPEND_INQUIRY_SCAN 0x02
#define HCI_HOLD_MODE_SUSPEND_PERIODIC_INQUIRIES 0x04

#define HCI_TO_HOST_FLOW_CTRL_ACL 0x01
#define HCI_TO_HOST_FLOW_CTRL_SCO 0x02

#define HCI_INQ_MODE_STD 0 /* normal mode @ BT 1.1+ */
#define HCI_INQ_MODE_RSSI 1 /* with RSSI   @ BT 1.2+ */
#define HCI_INQ_MODE_EIR 2 /* with EIR    @ BT 2.1+ */

#define HCI_SSP_KEY_ENTRY_STARTED 0
#define HCI_SSP_KEY_ENTRY_DIGIT_ENTERED 1
#define HCI_SSP_KEY_ENTRY_DIGIT_ERASED 2
#define HCI_SSP_KEY_ENTRY_CLEARED 3
#define HCI_SSP_KEY_ENTRY_COMPLETED 4

#define HCI_LOCATION_DOMAIN_OPTION_NONE 0x20 /* ' ' */
#define HCI_LOCATION_DOMAIN_OPTION_OUTDOORS_ONLY 0x4F /* 'O' */
#define HCI_LOCATION_DOMAIN_OPTION_INDOORS_ONLY 0x49 /* 'I' */
#define HCI_LOCATION_DOMAIN_OPTION_NON_COUNTRY_ENTITY 0x58 /* 'X' */

#define HCI_PERIOD_TYPE_DOWNLINK 0x00
#define HCI_PERIOD_TYPE_UPLINK 0x01
#define HCI_PERIOD_TYPE_BIDIRECTIONAL 0x02
#define HCI_PERIOD_TYPE_GUARD_PERIOD 0x03

#define HCI_MWS_INTERVAL_TYPE_NO_RX_NO_TX 0x00
#define HCI_MWS_INTERVAL_TYPE_TX_ALLOWED 0x01
#define HCI_MWS_INTERVAL_TYPE_RX_ALLOWED 0x02
#define HCI_MWS_INTERVAL_TYPE_TX_RX_ALLOWED 0x03
#define HCI_MWS_INTERVAL_TYPE_FRAME \
	0x04 /* type defined by Set External Frame Configuration command */

#define HCI_CONNLESS_FRAG_TYPE_CONT 0x00 /* continuation fragment */
#define HCI_CONNLESS_FRAG_TYPE_START 0x01 /* first fragment */
#define HCI_CONNLESS_FRAG_TYPE_END 0x02 /* last fragment */
#define HCI_CONNLESS_FRAG_TYPE_COMPLETE \
	0x03 /* complete fragment - no fragmentation */

#define HCI_CUR_MODE_ACTIVE 0x00
#define HCI_CUR_MODE_HOLD 0x01
#define HCI_CUR_MODE_SNIFF 0x02
#define HCI_CUR_MODE_PARK 0x03

#define HCI_SCO_LINK_TYPE_SCO 0x00
#define HCI_SCO_LINK_TYPE_ESCO 0x02

#define HCI_SCO_AIR_MODE_MULAW 0x00
#define HCI_SCO_AIR_MODE_ALAW 0x01
#define HCI_SCO_AIR_MODE_CVSD 0x02
#define HCI_SCO_AIR_MODE_TRANSPARENT 0x03

#define HCI_MCA_500_PPM 0x00
#define HCI_MCA_250_PPM 0x01
#define HCI_MCA_150_PPM 0x02
#define HCI_MCA_100_PPM 0x03
#define HCI_MCA_75_PPM 0x04
#define HCI_MCA_50_PPM 0x05
#define HCI_MCA_30_PPM 0x06
#define HCI_MCA_20_PPM 0x07

#define HCI_EDR_LINK_KEY_COMBO 0x00
#define HCI_EDR_LINK_KEY_LOCAL 0x01
#define HCI_EDR_LINK_KEY_REMOTE 0x02
#define HCI_EDR_LINK_KEY_DEBUG 0x03
#define HCI_EDR_LINK_KEY_UNAUTH_COMBO 0x04
#define HCI_EDR_LINK_KEY_AUTH_COMBO 0x05
#define HCI_EDR_LINK_KEY_CHANGED 0x06

#define HCI_VERSION_1_0_B 0 /* BT 1.0b */
#define HCI_VERSION_1_1 1 /* BT 1.1  */
#define HCI_VERSION_1_2 2 /* BT 1.2  */
#define HCI_VERSION_2_0 4 /* BT 2.0  */
#define HCI_VERSION_2_1 3 /* BT 2.1  */
#define HCI_VERSION_3_0 4 /* BT 3.0  */
#define HCI_VERSION_4_0 6 /* BT 4.0  */
#define HCI_VERSION_4_1 7 /* BT 4.1  */

#define HCI_LE_STATE_NONCON_ADV 0x0000000000000001ULL /* BT 4.0+ */
#define HCI_LE_STATE_SCANNABLE_ADV 0x0000000000000002ULL /* BT 4.0+ */
#define HCI_LE_STATE_CONNECTABLE_ADV 0x0000000000000004ULL /* BT 4.0+ */
#define HCI_LE_STATE_DIRECT_ADV 0x0000000000000008ULL /* BT 4.0+ */
#define HCI_LE_STATE_PASSIVE_SCAN 0x0000000000000010ULL /* BT 4.0+ */
#define HCI_LE_STATE_ACTIVE_SCAN 0x0000000000000020ULL /* BT 4.0+ */
#define HCI_LE_STATE_INITIATE 0x0000000000000040ULL /* BT 4.0+ */
#define HCI_LE_STATE_SLAVE 0x0000000000000080ULL /* BT 4.0+ */
#define HCI_LE_STATE_NONCON_ADV_w_PASSIVE_SCAN \
	0x0000000000000100ULL /* BT 4.0+ */
#define HCI_LE_STATE_SCANNABLE_ADV_w_PASSIVE_SCAN \
	0x0000000000000200ULL /* BT 4.0+ */
#define HCI_LE_STATE_CONNECTABLE_ADV_w_PASSIVE_SCAN \
	0x0000000000000400ULL /* BT 4.0+ */
#define HCI_LE_STATE_DIRECT_ADV_w_PASSIVE_SCAN \
	0x0000000000000800ULL /* BT 4.0+ */
#define HCI_LE_STATE_NONCON_ADV_w_ACTIVE_SCAN \
	0x0000000000001000ULL /* BT 4.0+ */
#define HCI_LE_STATE_SCANNABLE_ADV_w_ACTIVE_SCAN \
	0x0000000000002000ULL /* BT 4.0+ */
#define HCI_LE_STATE_CONNECTABLE_ADV_w_ACTIVE_SCAN \
	0x0000000000004000ULL /* BT 4.0+ */
#define HCI_LE_STATE_DIRECT_ADV_w_ACTIVE_SCAN \
	0x0000000000008000ULL /* BT 4.0+ */
#define HCI_LE_STATE_NONCON_ADV_w_INITIATING \
	0x0000000000010000ULL /* BT 4.0+     \
			       */
#define HCI_LE_STATE_SCANNABLE_ADV_w_INITIATING \
	0x0000000000020000ULL /* BT 4.0+ */
#define HCI_LE_STATE_NONCON_ADV_w_MASTER 0x0000000000040000ULL /* BT 4.0+ */
#define HCI_LE_STATE_SCANNABLE_ADV_w_MASTER \
	0x0000000000080000ULL /* BT 4.0+    \
			       */
#define HCI_LE_STATE_NONCON_ADV_w_SLAVE 0x0000000000100000ULL /* BT 4.0+ */
#define HCI_LE_STATE_SCANNABLE_ADV_w_SLAVE 0x0000000000200000ULL /* BT 4.0+ */
#define HCI_LE_STATE_PASSIVE_SCAN_w_INITIATING \
	0x0000000000400000ULL /* BT 4.0+ */
#define HCI_LE_STATE_ACTIVE_SCAN_w_INITIATING \
	0x0000000000800000ULL /* BT 4.0+ */
#define HCI_LE_STATE_PASSIVE_SCAN_w_MASTER 0x0000000001000000ULL /* BT 4.0+ */
#define HCI_LE_STATE_ACTIVE_SCAN_w_MASTER 0x0000000002000000ULL /* BT 4.0+ */
#define HCI_LE_STATE_PASSIVE_SCAN_w_SLAVE 0x0000000004000000ULL /* BT 4.0+ */
#define HCI_LE_STATE_ACTIVE_SCAN_w_SLAVE 0x0000000008000000ULL /* BT 4.0+ */
#define HCI_LE_STATE_INTIATING_w_MASTER 0x0000000010000000ULL /* BT 4.0+ */
#define HCI_LE_STATE_LOW_DUTY_CYCLE_DIRECT_ADV \
	0x0000000020000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_PASSIVE_SCAN_w_LOW_DUTY_CYCLE_DIRECT_ADV \
	0x0000000040000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_ACTIVE_SCAN_w_LOW_DUTY_CYCLE_DIRECT_ADV \
	0x0000000080000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_CONNECTABLE_ADV_w_INITIATING \
	0x0000000100000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_DIRECT_ADV_w_INITIATING \
	0x0000000200000000ULL /* BT 4.1+     \
			       */
#define HCI_LE_STATE_LOW_DUTY_CYCLE_DIRECT_ADV_w_INITIATING \
	0x0000000400000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_CONNECTABLE_ADV_w_MASTER \
	0x0000000800000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_DIRECT_ADV_w_MASTER 0x0000001000000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_LOW_DUTY_CYCLE_DIRECT_ADV_w_MASTER \
	0x0000002000000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_CONNECTABLE_ADV_w_SLAVE \
	0x0000004000000000ULL /* BT 4.1+     \
			       */
#define HCI_LE_STATE_DIRECT_ADV_w_SLAVE 0x0000008000000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_LOW_DUTY_CYCLE_DIRECT_ADV_w_SLAVE \
	0x0000010000000000ULL /* BT 4.1+ */
#define HCI_LE_STATE_INITIATING_w_SLAVE 0x0000020000000000ULL /* BT 4.1+ */

#define HCI_LMP_FTR_3_SLOT_PACKETS 0x0000000000000001ULL /* BT 1.1+ */
#define HCI_LMP_FTR_5_SLOT_PACKETS 0x0000000000000002ULL /* BT 1.1+ */
#define HCI_LMP_FTR_ENCRYPTION 0x0000000000000004ULL /* BT 1.1+ */
#define HCI_LMP_FTR_SLOT_OFFSET 0x0000000000000008ULL /* BT 1.1+ */
#define HCI_LMP_FTR_TIMING_ACCURACY 0x0000000000000010ULL /* BT 1.1+ */
#define HCI_LMP_FTR_SWITCH 0x0000000000000020ULL /* BT 1.1+ */
#define HCI_LMP_FTR_HOLD_MODE 0x0000000000000040ULL /* BT 1.1+ */
#define HCI_LMP_FTR_SNIFF_MODE 0x0000000000000080ULL /* BT 1.1+ */
#define HCI_LMP_FTR_PARK_MODE 0x0000000000000100ULL /* BT 1.1+ */
#define HCI_LMP_FTR_RSSI 0x0000000000000200ULL /* BT 1.1+ */
#define HCI_LMP_FTR_CHANNEL_QUALITY_DRIVEN_DATA_RATE \
	0x0000000000000400ULL /* BT 1.1+ */
#define HCI_LMP_FTR_SCO_LINKS 0x0000000000000800ULL /* BT 1.1+ */
#define HCI_LMP_FTR_HV2_PACKETS 0x0000000000001000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_HV3_PACKETS 0x0000000000002000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_MU_LAW 0x0000000000004000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_A_LAW 0x0000000000008000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_CVSD 0x0000000000010000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_PAGING_SCHEME 0x0000000000020000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_POWER_CONTROL 0x0000000000040000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_TRANSPARENT_SCO_DATA 0x0000000000080000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_FLOW_CONTROL_LAG_B0 0x0000000000100000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_FLOW_CONTROL_LAG_B1 0x0000000000200000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_FLOW_CONTROL_LAG_B2 0x0000000000400000ULL /* BT 1.1+ */
#define HCI_LMP_FTR_BROADCAST_ENCRYPTION 0x0000000000800000ULL /* BT 1.2+ */
#define HCI_LMP_FTR_ACL_2MBPS 0x0000000002000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ACL_3MBPS 0x0000000004000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ENHANCED_INQUIRY_SCAN 0x0000000008000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_INTERLACED_INQUIRY_SCAN \
	0x0000000010000000ULL /* BT 2.1+    \
			       */
#define HCI_LMP_FTR_INTERLACED_PAGE_SCAN 0x0000000020000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_RSSI_WITH_INQUIRY_RESULTS \
	0x0000000040000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_EXTENDED_SCO_LINK \
	0x0000000080000000ULL /* BT 2.1+ */ /* EV3 packets */
#define HCI_LMP_FTR_EV4_PACKETS 0x0000000100000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_EV5_PACKETS 0x0000000200000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_AFH_CAPABLE_SLAVE 0x0000000800000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_AFH_CLASSIFICATION_SLAVE \
	0x0000001000000000ULL /* BT 2.1+     \
			       */
#define HCI_LMP_FTR_BR_EDR_NOT_SUPPORTED 0x0000002000000000ULL /* BT 4.0+ */
#define HCI_LMP_FTR_LE_SUPPORTED_CONTROLLER \
	0x0000004000000000ULL /* BT 4.0+    \
			       */
#define HCI_LMP_FTR_3_SLOT_ACL_PACKETS 0x0000008000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_5_SLOT_ACL_PACKETS 0x0000010000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_SNIFF_SUBRATING 0x0000020000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_PAUSE_ENCRYPTION 0x0000040000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_AFH_CAPABLE_MASTER 0x0000080000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_AFH_CLASSIFICATION_MASTER \
	0x0000100000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ESCO_2MBPS 0x0000200000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ESCO_3MBPS 0x0000400000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_3_SLOT_ESCO 0x0000800000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_EXTENDED_INQUIRY_RESPONSE \
	0x0001000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_SSP 0x0008000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ENCAPSULATED_PDU 0x0010000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ERRONEOUS_DATA_REPORTING \
	0x0020000000000000ULL /* BT 2.1+     \
			       */
#define HCI_LMP_FTR_NON_FLUSHABLE_PACKET_BOUNDARY_FLAG \
	0x0040000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_LINK_SUPERVISION_TIMEOUT_CHANGED_EVENT \
	0x0100000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_INQUIRY_RESPONSE_TX_POWER_LEVEL \
	0x0200000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_EXTENDED_FEATURES 0x8000000000000000ULL /* BT 2.1+ */
#define HCI_LMP_FTR_ENHANCED_POWER_CONTROL 0x0400000000000000ULL /* BT 3.0+ */
#define HCI_LMP_FTR_SIMUL_LE_EDR_CAPABLE_CONTROLLER \
	0x0002000000000000ULL /* BT 4.0+ */

#define HCI_LMP_EXT_FTR_P1_SSP_HOST_SUPPORT \
	0x0000000000000001ULL /* BT 2.1+    \
			       */
#define HCI_LMP_EXT_FTR_P1_LE_HOST_SUPPORT 0x0000000000000002ULL /* BT 4.0+ */
#define HCI_LMP_EXT_FTR_P1_SIMUL_LE_EDR_HOST_SUPPORT \
	0x0000000000000004ULL /* BT 4.0+ */
#define HCI_LMP_EXT_FTR_P1_SECURE_CONNECTIONS_HOST_SUPPORT \
	0x0000000000000008ULL /* BT 4.1+ */

#define HCI_LMP_EXT_FTR_P2_CONNLESS_SLAVE_BROADCAST_MASTER \
	0x0000000000000001ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_CONNLESS_SLAVE_BROADCAST_SLAVE \
	0x0000000000000002ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_SYNCHRONIZATION_TRAIN \
	0x0000000000000004ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_SYNCHRONIZATION_SCAN \
	0x0000000000000008ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_INQUIRY_RESPONSE_NOTIFICATION_EVT \
	0x0000000000000010ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_GENERALIZED_INTERLACED_SCAN \
	0x0000000000000020ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_COARSE_CLOCK_ADJUSTMENT \
	0x0000000000000040ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_SECURE_CONNECTIONS_CAPABLE_CONTROLLER \
	0x0000000000000100ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_PING 0x0000000000000200ULL /* BT 4.1+ */
#define HCI_LMP_EXT_FTR_P2_TRAIN_NUDGING 0x0000000000000800ULL /* BT 4.1+ */

#define HCI_EVENT_INQUIRY_COMPLETE 0x0000000000000001ULL /* BT 1.1+ */
#define HCI_EVENT_INQUIRY_RESULT 0x0000000000000002ULL /* BT 1.1+ */
#define HCI_EVENT_CONN_COMPLETE 0x0000000000000004ULL /* BT 1.1+ */
#define HCI_EVENT_CONN_REQUEST 0x0000000000000008ULL /* BT 1.1+ */
#define HCI_EVENT_DISCONNECTION_COMPLETE 0x0000000000000010ULL /* BT 1.1+ */
#define HCI_EVENT_AUTH_COMPLETE 0x0000000000000020ULL /* BT 1.1+ */
#define HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE \
	0x0000000000000040ULL /* BT 1.1+ */
#define HCI_EVENT_ENCR_CHANGE 0x0000000000000080ULL /* BT 1.1+ */
#define HCI_EVENT_CHANGE_CONN_LINK_KEY_COMPLETE \
	0x0000000000000100ULL /* BT 1.1+ */
#define HCI_EVENT_MASTER_LINK_KEY_COMPLETE 0x0000000000000200ULL /* BT 1.1+ */
#define HCI_EVENT_READ_REMOTE_SUPPORTED_FEATURES_COMPLETE \
	0x0000000000000400ULL /* BT 1.1+ */
#define HCI_EVENT_READ_REMOTE_VERSION_INFO_COMPLETE \
	0x0000000000000800ULL /* BT 1.1+ */
#define HCI_EVENT_QOS_SETUP_COMPLETE 0x0000000000001000ULL /* BT 1.1+ */
#define HCI_EVENT_HARDWARE_ERROR 0x0000000000008000ULL /* BT 1.1+ */
#define HCI_EVENT_FLUSH_OCCURRED 0x0000000000010000ULL /* BT 1.1+ */
#define HCI_EVENT_ROLE_CHANGE 0x0000000000020000ULL /* BT 1.1+ */
#define HCI_EVENT_MODE_CHANGE 0x0000000000080000ULL /* BT 1.1+ */
#define HCI_EVENT_RETURN_LINK_KEYS 0x0000000000100000ULL /* BT 1.1+ */
#define HCI_EVENT_PIN_CODE_REQUEST 0x0000000000200000ULL /* BT 1.1+ */
#define HCI_EVENT_LINK_KEY_REQUEST 0x0000000000400000ULL /* BT 1.1+ */
#define HCI_EVENT_LINK_KEY_NOTIFICATION 0x0000000000800000ULL /* BT 1.1+ */
#define HCI_EVENT_LOOPBACK_COMMAND 0x0000000001000000ULL /* BT 1.1+ */
#define HCI_EVENT_DATA_BUFFER_OVERFLOW 0x0000000002000000ULL /* BT 1.1+ */
#define HCI_EVENT_MAX_SLOTS_CHANGE 0x0000000004000000ULL /* BT 1.1+ */
#define HCI_EVENT_READ_CLOCK_OFFSET_COMPLETE \
	0x0000000008000000ULL /* BT 1.1+     \
			       */
#define HCI_EVENT_CONN_PACKET_TYPE_CHANGED 0x0000000010000000ULL /* BT 1.1+ */
#define HCI_EVENT_QOS_VIOLATION 0x0000000020000000ULL /* BT 1.1+ */
#define HCI_EVENT_PAGE_SCAN_MODE_CHANGE \
	0x0000000040000000ULL /* BT 1.1+, obsolete @ BT1.2+ */
#define HCI_EVENT_PAGE_SCAN_REPETITION_MODE_CHANGE \
	0x0000000080000000ULL /* BT 1.1+ */
#define HCI_EVENT_ALL_BT_1_1 \
	0x00000000FFFFFFFFULL /* also the default for BT 1.1 */
#define HCI_EVENT_FLOW_SPEC_COMPLETE 0x0000000100000000ULL /* BT 1.2+ */
#define HCI_EVENT_INQUIRY_RESULT_WITH_RSSI 0x0000000200000000ULL /* BT 1.2+ */
#define HCI_EVENT_READ_REMOTE_EXTENDED_FEATURES_COMPLETE \
	0x0000000400000000ULL /* BT 1.2+ */
#define HCI_EVENT_SYNC_CONN_COMPLETE 0x0000080000000000ULL /* BT 1.2+ */
#define HCI_EVENT_SYNC_CONN_CHANGED 0x0000100000000000ULL /* BT 1.2+ */
#define HCI_EVENT_ALL_BT_1_2 \
	0x00001FFFFFFFFFFFULL /* also the default for BT 1.2+ */
#define HCI_EVENT_SNIFF_SUBRATING 0x0000200000000000ULL /* BT 2.1+ */
#define HCI_EVENT_EXTENDED_INQUIRY_RESULT 0x0000400000000000ULL /* BT 2.1+ */
#define HCI_EVENT_ENCR_KEY_REFRESH_COMPLETE \
	0x0000800000000000ULL /* BT 2.1+    \
			       */
#define HCI_EVENT_IO_CAPABILITY_REQUEST 0x0001000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_IO_CAPABILITY_REQUEST_REPLY \
	0x0002000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_USER_CONFIRMATION_REQUEST \
	0x0004000000000000ULL /* BT 2.1+    \
			       */
#define HCI_EVENT_USER_PASSKEY_REQUEST 0x0008000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_REMOTE_OOB_DATA_REQUEST 0x0010000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_SIMPLE_PAIRING_COMPLETE 0x0020000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_LINK_SUPERVISION_TIMOUT_CHANGED \
	0x0080000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_ENHANCED_FLUSH_COMPLETE 0x0100000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_USER_PASSKEY_NOTIFICATION \
	0x0400000000000000ULL /* BT 2.1+    \
			       */
#define HCI_EVENT_KEYPRESS_NOTIFICATION 0x0800000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_REMOTE_HOST_SUPPORTED_FEATURES \
	0x1000000000000000ULL /* BT 2.1+ */
#define HCI_EVENT_ALL_BT_2_1 0x1DBFFFFFFFFFFFFFULL
#define HCI_EVENT_ALL_BT_3_0 0x1DBFFFFFFFFFFFFFULL
#define HCI_EVENT_LE_META 0x2000000000000000ULL /* BT 4.0+ */
#define HCI_EVENT_ALL_BT_4_0 0x3DBFFFFFFFFFFFFFULL
#define HCI_EVENT_ALL_BT_4_1 0x3DBFFFFFFFFFFFFFULL

#define HCI_EVENT_P2_PHYS_LINK_COMPLETE 0x0000000000000001ULL /* BT 3.0+ */
#define HCI_EVENT_P2_CHANNEL_SELECTED 0x0000000000000002ULL /* BT 3.0+ */
#define HCI_EVENT_P2_DISCONNECTION_PHYSICAL_LINK \
	0x0000000000000004ULL /* BT 3.0+ */
#define HCI_EVENT_P2_PHYSICAL_LINK_LOSS_EARLY_WARNING \
	0x0000000000000008ULL /* BT 3.0+ */
#define HCI_EVENT_P2_PHYSICAL_LINK_RECOVERY \
	0x0000000000000010ULL /* BT 3.0+    \
			       */
#define HCI_EVENT_P2_LOGICAL_LINK_COMPLETE 0x0000000000000020ULL /* BT 3.0+ */
#define HCI_EVENT_P2_DISCONNECTION_LOGICAL_LINK_COMPLETE \
	0x0000000000000040ULL /* BT 3.0+ */
#define HCI_EVENT_P2_FLOW_SPEC_MODIFY_COMPLETE \
	0x0000000000000080ULL /* BT 3.0+ */
#define HCI_EVENT_P2_NUMBER_OF_COMPLETED_DATA_BLOCKS \
	0x0000000000000100ULL /* BT 3.0+ */
#define HCI_EVENT_P2_AMP_START_TEST 0x0000000000000200ULL /* BT 3.0+ */
#define HCI_EVENT_P2_AMP_TEST_END 0x0000000000000400ULL /* BT 3.0+ */
#define HCI_EVENT_P2_AMP_RECEIVER_REPORT 0x0000000000000800ULL /* BT 3.0+ */
#define HCI_EVENT_P2_SHORT_RANGE_MODE_CHANGE_COMPLETE \
	0x0000000000001000ULL /* BT 3.0+ */
#define HCI_EVENT_P2_AMP_STATUS_CHANGE 0x0000000000002000ULL /* BT 3.0+ */
#define HCI_EVENT_P2_ALL_BT_3_0 0x0000000000003FFFULL
#define HCI_EVENT_P2_ALL_BT_4_0 0x0000000000003FFFULL
#define HCI_EVENT_P2_TRIGGERED_CLOCK_CAPTURE \
	0x0000000000004000ULL /* BT 4.1+     \
			       */
#define HCI_EVENT_P2_SYNCH_TRAIN_COMPLETE 0x0000000000008000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_SYNCH_TRAIN_RECEIVED 0x0000000000010000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_CONNLESS_SLAVE_BROADCAST_RXED \
	0x0000000000020000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_CONNLESS_SLAVE_BROADCAST_TIMEOUT \
	0x0000000000040000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_TRUNCATED_PAGE_COMPLETE \
	0x0000000000080000ULL /* BT 4.1+     \
			       */
#define HCI_EVENT_P2_SLAVE_PAGE_RESPONSE_TIMEOUT \
	0x0000000000100000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_CONNLESS_SLAVE_BROADCAST_CHANNEL_MAP_CHANGE \
	0x0000000000200000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_INQUIRY_RESPONSE_NOTIFICATION \
	0x0000000000400000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_AUTHENTICATED_PAYLOAD_TIMEOUT_EXPIRED \
	0x0000000000800000ULL /* BT 4.1+ */
#define HCI_EVENT_P2_ALL_BT_4_1 0x0000000000FFFFFFULL

#define HCI_LE_EVENT_CONN_COMPLETE 0x0000000000000001ULL /* BT 4.0+ */
#define HCI_LE_EVENT_ADV_REPORT 0x0000000000000002ULL /* BT 4.0+ */
#define HCI_LE_EVENT_CONN_UPDATE_COMPLETE 0x0000000000000004ULL /* BT 4.0+ */
#define HCI_LE_EVENT_READ_REMOTE_USED_FEATURES_CMPLETE \
	0x0000000000000008ULL /* BT 4.0+ */
#define HCI_LE_EVENT_LTK_REQUEST 0x0000000000000010ULL /* BT 4.0+ */
#define HCI_LE_EVENT_REMOTE_CONNECTION_PARAMETER_REQUEST \
	0x0000000000000020ULL /* BT 4.1+ */

#define HCI_LE_FTR_ENCRYPTION 0x0000000000000001ULL /* BT 4.0+ */
#define HCI_LE_FTR_CONNECTION_PARAMETERS_REQUEST \
	0x0000000000000002ULL /* BT 4.1+ */
#define HCI_LE_FTR_EXTENDED_REJECT_INDICATION \
	0x0000000000000004ULL /* BT 4.1+ */
#define HCI_LE_FTR_SLAVE_INITIATED_FEATURES_EXCHANGE \
	0x0000000000000008ULL /* BT 4.1+ */
#define HCI_LE_FTR_LE_PING 0x0000000000000010ULL /* BT 4.1+ */

#define HCI_OGF_Link_Control 1

/* ==== BT 1.1 ==== */

#define HCI_CMD_Inquiry 0x0001 /* status */
struct hciInquiry {
	uint8_t lap[3];
	uint8_t inqLen;
	uint8_t numResp;
} __packed;

#define HCI_CMD_Inquiry_Cancel 0x0002 /* complete */
struct hciCmplInquiryCancel {
	uint8_t status;
} __packed;

#define HCI_CMD_Periodic_Inquiry_Mode 0x0003 /* complete */
struct hciPeriodicInquiryMode {
	uint16_t maxPeriodLen;
	uint16_t minPeriodLen;
	uint8_t lap[3];
	uint8_t inqLen;
	uint8_t numResp;
} __packed;
struct hciCmplPeriodicInquiryMode {
	uint8_t status;
} __packed;

#define HCI_CMD_Exit_Periodic_Inquiry_Mode 0x0004 /* complete */

#define HCI_CMD_Create_Connection 0x0005 /* status */
struct hciCreateConnection {
	uint8_t mac[6];
	uint16_t allowedPackets; /* HCI_PKT_TYP_* */
	uint8_t PSRM;
	uint16_t clockOffset; /* possibly | HCI_CLOCK_OFST_VALID */
	uint8_t allowRoleSwitch;
} __packed;

#define HCI_CMD_Disconnect 0x0006 /* status */
struct hciDisconnect {
	uint16_t conn;
	uint8_t reason;
} __packed;

#define HCI_CMD_Add_SCO_Connection \
	0x0007 /* status */ /* deprecated in BT 1.2+ */
struct hciAddScoConnection {
	uint16_t conn;
	uint16_t packetTypes; /* HCI_PKT_TYP_SCO_* */
} __packed;

#define HCI_CMD_Create_Connection_Cancel 0x0008 /* complete */
struct hciCreateConnectionCancel {
	uint8_t mac[6];
} __packed;
struct hciCmplCreateConnectionCancel {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Accept_Connection_Request 0x0009 /* status */
struct hciAcceptConnection {
	uint8_t mac[6];
	uint8_t remainSlave;
} __packed;

#define HCI_CMD_Reject_Connection_Request 0x000A /* status */
struct hciRejectConnection {
	uint8_t mac[6];
	uint8_t reason;
} __packed;

#define HCI_CMD_Link_Key_Request_Reply 0x000B /* complete */
struct hciLinkKeyRequestReply {
	uint8_t mac[6];
	uint8_t key[16];
} __packed;
struct hciCmplLinkKeyRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Link_Key_Request_Negative_Reply 0x000C /* complete */
struct hciLinkKeyRequestNegativeReply {
	uint8_t mac[6];
} __packed;
struct hciCmplLinkKeyRequestNegativeReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_PIN_Code_Request_Reply 0x000D /* complete */
struct hciPinCodeRequestReply {
	uint8_t mac[6];
	uint8_t pinCodeLen;
	uint8_t pinCode[16];
} __packed;
struct hciCmplPinCodeRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_PIN_Code_Request_Negative_Reply 0x000E /* complete */
struct hciPinCodeRequestNegativeReply {
	uint8_t mac[6];
} __packed;
struct hciCmplPinCodeRequestNegativeReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Change_Connection_Packet_Type 0x000F /* status */
struct hciChangeConnectionPacketType {
	uint16_t conn;
	uint16_t allowedPackets; /* HCI_PKT_TYP_* */
} __packed;

#define HCI_CMD_Authentication_Requested 0x0011 /* status */
struct hciAuthRequested {
	uint16_t conn;
} __packed;

#define HCI_CMD_Set_Connection_Encryption 0x0013 /* status */
struct hciSetConnectionEncryption {
	uint16_t conn;
	uint8_t encrOn;
} __packed;

#define HCI_CMD_Change_Connection_Link_Key 0x0015 /* status */
struct hciChangeConnLinkKey {
	uint16_t conn;
} __packed;

#define HCI_CMD_Master_Link_Key 0x0017 /* status */
struct hciMasterLinkKey {
	uint8_t useTempKey;
} __packed;

#define HCI_CMD_Remote_Name_Request 0x0019 /* status */
struct hciRemoteNameRequest {
	uint8_t mac[6];
	uint8_t PSRM;
	uint8_t PSM; /* deprecated, should be zero for BT 1.2+ */
	uint16_t clockOffset; /* possibly | HCI_CLOCK_OFST_VALID */
} __packed;

#define HCI_CMD_Remote_Name_Request_Cancel 0x001A /* complete */
struct hciRemoteNameRequestCancel {
	uint8_t mac[6];
} __packed;
struct hciCmplRemoteNameRequestCancel {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Read_Remote_Supported_Features 0x001B /* status */
struct hciReadRemoteSupportedFeatures {
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Remote_Version_Information 0x001D /* status */
struct hciReadRemoteVersionInfo {
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Clock_Offset 0x001F /* status */
struct hciReadClockOffset {
	uint16_t conn;
} __packed;

/* ==== BT 1.2 ==== */

#define HCI_CMD_Read_Remote_Extended_Features 0x001C /* status */
struct hciReadRemoteExtendedFeatures {
	uint16_t conn;
	uint8_t page; /* BT1.2 max: 0 */
} __packed;

#define HCI_CMD_Read_Lmp_Handle 0x0020 /* complete */
struct hciReadLmpHandle {
	uint16_t handle;
} __packed;
struct hciCmplReadLmpHandle {
	uint8_t status;
	uint16_t handle;
	uint8_t lmpHandle;
	uint32_t reserved;
} __packed;

#define HCI_CMD_Setup_Synchronous_Connection 0x0028 /* status */
struct hciSetupSyncConn {
	uint16_t conn;
	uint32_t txBandwidth;
	uint32_t rxBandwidth;
	uint16_t maxLatency;
	uint16_t voiceSetting;
	uint8_t retransmissionEffort;
	uint16_t allowedPacketsSco; /* HCI_PKT_TYP_SCO_* */
} __packed;

#define HCI_CMD_Accept_Synchronous_Connection_Request 0x0029 /* status */
struct hciAcceptSyncConn {
	uint8_t mac[6];
	uint32_t txBandwidth;
	uint32_t rxBandwidth;
	uint16_t maxLatency;
	uint16_t contentFormat;
	uint8_t retransmissionEffort;
	uint16_t allowedPacketsSco; /* HCI_PKT_TYP_SCO_* */
} __packed;

#define HCI_CMD_Reject_Synchronous_Connection_Request 0x002A /* status */
struct hciRejectSyncConn {
	uint8_t mac[6];
	uint8_t reason;
} __packed;

/* ==== BR 2.1 ==== */

#define HCI_CMD_IO_Capability_Request_Reply 0x002B /* complete */
struct hciIoCapabilityRequestReply {
	uint8_t mac[6];
	uint8_t cap; /* HCI_DISPLAY_CAP_* */
	uint8_t oobPresent;
	uint8_t authReqments; /* HCI_AUTH_REQMENT_* */
} __packed;
struct hciCmplIoCapabilityRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_User_Confirmation_Request_Reply 0x002C /* complete */
struct hciUserConfRequestReply {
	uint8_t mac[6];
} __packed;
struct hciCmplUserConfRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_User_Confirmation_Request_Negative_Reply 0x002D /* complete */
struct hciUserConfRequestNegativeReply {
	uint8_t mac[6];
} __packed;
struct hciCmplUserConfRequestNegativeReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_User_Passkey_Request_Reply 0x002E /* complete */
struct hciUserPasskeyRequestReply {
	uint8_t mac[6];
	uint32_t num;
} __packed;
struct hciCmplUserPasskeyRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_User_Passkey_Request_Negative_Reply 0x002F /* complete */
struct hciUserPasskeyRequestNegativeReply {
	uint8_t mac[6];
} __packed;
struct hciCmplUserPasskeyRequestNegativeReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Remote_OOB_Data_Request_Reply 0x0030 /* complete */
struct hciRemoteOobDataRequestReply {
	uint8_t mac[6];
	uint8_t C[16];
	uint8_t R[16];
} __packed;
struct hciCmplRemoteOobDataRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Remote_OOB_Data_Request_Negative_Reply 0x0033 /* complete */
struct hciRemoteOobDataRequestNegativeReply {
	uint8_t mac[6];
} __packed;
struct hciCmplRemoteOobDataRequestNegativeReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_IO_Capability_Request_Negative_Reply 0x0034 /* complete */
struct hciIoCapabilityRequestNegativeReply {
	uint8_t mac[6];
	uint8_t reason;
} __packed;
struct hciCmplIoCapabilityRequestNegativeReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

/* ==== BT 3.0 ==== */

#define HCI_CMD_Create_Physical_link 0x0035 /* status */
struct hciCreatePhysicalLink {
	uint8_t physLinkHandle;
	uint8_t dedicatedAmpKeyLength;
	uint8_t dedicatedAmpKeyType;
	uint8_t dedicatedAmpKey;
} __packed;

#define HCI_CMD_Accept_Physical_link 0x0036 /* status */
struct hciAcceptPhysicalLink {
	uint8_t physLinkHandle;
	uint8_t dedicatedAmpKeyLength;
	uint8_t dedicatedAmpKeyType;
	uint8_t dedicatedAmpKey;
} __packed;

#define HCI_CMD_Disconnect_Physical_link 0x0037 /* status */
struct hciDisconnectPhysicalLink {
	uint8_t physLinkHandle;
	uint8_t reason;
} __packed;

#define HCI_CMD_Create_Logical_link 0x0038 /* status */
struct hciCreateLogicalLink {
	uint8_t physLinkHandle;
	uint8_t txFlowSpec[16];
	uint8_t rxFlowSpec[16];
} __packed;

#define HCI_CMD_Accept_Logical_Link 0x0039 /* status */
struct hciAcceptLogicalLink {
	uint8_t physLinkHandle;
	uint8_t txFlowSpec[16];
	uint8_t rxFlowSpec[16];
} __packed;

#define HCI_CMD_Disconnect_Logical_link 0x003A /* status */
struct hciDisconnectLogicalLink {
	uint8_t physLinkHandle;
} __packed;

#define HCI_CMD_Logical_Link_Cancel 0x003B /* complete */
struct hciLogicalLinkCancel {
	uint8_t physLinkHandle;
	uint8_t txFlowSpecID;
} __packed;
struct hciCmplLogicalLinkCancel {
	uint8_t status;
	uint8_t physLinkHandle;
	uint8_t txFlowSpecID;
} __packed;

#define HCI_CMD_Flow_Spec_Modify 0x003C /* status */
struct hciFlowSpecModify {
	uint16_t handle;
	uint8_t txFlowSpec[16];
	uint8_t rxFlowSpec[16];
} __packed;

/* ==== BT 4.1 ==== */

#define HCI_CMD_Enhanced_Setup_Synchronous_Connection 0x003D /* status */
struct hciEnhSetupSyncConn {
	uint16_t conn;
	uint32_t txBandwidth;
	uint32_t rxBandwidth;
	uint8_t txCodingFormat[5];
	uint8_t rxCodingFormat[5];
	uint16_t txCodecFrameSize;
	uint16_t rxCodecFrameSize;
	uint32_t inputBandwidth;
	uint32_t outputBandwidth;
	uint8_t inputCodingFormat[5];
	uint8_t outputCodingFormat[5];
	uint16_t inputCodedDataSize;
	uint16_t outputCodedDataSize;
	uint8_t inputPcmDataFormat;
	uint8_t outputPcmDataFormat;
	uint8_t inputPcmSamplePayloadMsbPosition;
	uint8_t outputPcmSamplePayloadMsbPosition;
	uint8_t inputDataPath;
	uint8_t outputDataPath;
	uint8_t inputTransportUnitSize;
	uint8_t outputTransportUnitSize;
	uint16_t maxLatency;
	uint16_t allowedPacketsSco; /* HCI_PKT_TYP_SCO_* */
	uint8_t retransmissionEffort;
} __packed;

#define HCI_CMD_Enhanced_Accept_Synchronous_Connection 0x003E /* status */
struct hciEnhAcceptSyncConn {
	uint8_t mac[6];
	uint32_t txBandwidth;
	uint32_t rxBandwidth;
	uint8_t txCodingFormat[5];
	uint8_t rxCodingFormat[5];
	uint16_t txCodecFrameSize;
	uint16_t rxCodecFrameSize;
	uint32_t inputBandwidth;
	uint32_t outputBandwidth;
	uint8_t inputCodingFormat[5];
	uint8_t outputCodingFormat[5];
	uint16_t inputCodedDataSize;
	uint16_t outputCodedDataSize;
	uint8_t inputPcmDataFormat;
	uint8_t outputPcmDataFormat;
	uint8_t inputPcmSamplePayloadMsbPosition;
	uint8_t outputPcmSamplePayloadMsbPosition;
	uint8_t inputDataPath;
	uint8_t outputDataPath;
	uint8_t inputTransportUnitSize;
	uint8_t outputTransportUnitSize;
	uint16_t maxLatency;
	uint16_t allowedPacketsSco; /* HCI_PKT_TYP_SCO_* */
	uint8_t retransmissionEffort;
} __packed;

#define HCI_CMD_Truncated_Page 0x003F /* status */
struct hciTruncatedPage {
	uint8_t mac[6];
	uint8_t PSRM;
	uint16_t clockOffset; /* possibly | HCI_CLOCK_OFST_VALID */
} __packed;

#define HCI_CMD_Truncated_Page_Cancel 0x0040 /* complete */
struct hciTruncatedPageCancel {
	uint8_t mac[6];
} __packed;
struct hciCmplTruncatedPageCancel {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_CMD_Set_Connectionless_Slave_Broadcast 0x0041 /* complete */
struct hciSetConnectionlessSlaveBroadcast {
	uint8_t enabled;
	uint8_t ltAddr; /* 1..7 */
	uint8_t lpoAllowed; /* can sleep? */
	uint16_t allowedPackets; /* HCI_PKT_TYP_* */
	uint16_t intervalMin;
	uint16_t intervalMax;
	uint16_t supervisionTimeout;
} __packed;
struct hciCmplSetConnectionlessSlaveBroadcast {
	uint8_t status;
	uint8_t ltAddr; /* 1..7 */
	uint16_t interval;
} __packed;

#define HCI_CMD_Set_Connectionless_Slave_Broadcast_Receive \
	0x0042 /* complete                                 \
		*/
struct hciSetConnectionlessSlaveBroadcastReceive {
	uint8_t enabled;
	uint8_t mac[6]; /* add rof tranmitter */
	uint8_t ltAddr; /* 1..7 */
	uint16_t interval;
	uint32_t clockOffset; /* lower 28 bits used */
	uint32_t nextConnectionlessSlaveBroadcastClock; /* lower 28 bits used */
	uint16_t supervisionTimeout;
	uint8_t remoteTimingAccuracy;
	uint8_t skip;
	uint16_t allowedPackets; /* HCI_PKT_TYP_* */
	uint8_t afhChannelMap[10];
} __packed;
struct hciCmplSetConnectionlessSlaveBroadcastReceive {
	uint8_t status;
	uint8_t mac[6]; /* add rof tranmitter */
	uint8_t ltAddr; /* 1..7 */
} __packed;

#define HCI_CMD_Start_Synchronisation_Train 0x0043 /* status */

#define HCI_CMD_Receive_Synchronisation_Train 0x0044 /* status */
struct hciReceiveSyncTrain {
	uint8_t mac[6];
	uint16_t syncScanTimeout;
	uint16_t syncScanWindow;
	uint16_t syncScanInterval;
} __packed;

#define HCI_CMD_Remote_OOB_Extended_Data_Request_Reply 0x0045 /* complete */
struct hciRemoteOobExtendedDataRequestReply {
	uint8_t mac[6];
	uint8_t C_192[16];
	uint8_t R_192[16];
	uint8_t C_256[16];
	uint8_t R_256[16];
} __packed;
struct hciCmplRemoteOobExtendedDataRequestReply {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_OGF_Link_Policy 2

/* ==== BT 1.1 ==== */

#define HCI_CMD_Hold_Mode 0x0001 /* status */
struct hciHoldMode {
	uint16_t conn;
	uint16_t holdModeMaxInt;
	uint16_t holdModeMinInt;
} __packed;

#define HCI_CMD_Sniff_Mode 0x0003 /* status */
struct hciSniffMode {
	uint16_t conn;
	uint16_t sniffMaxInt;
	uint16_t sniffMinInt;
	uint16_t sniffAttempt;
	uint16_t sniffTimeout;
} __packed;

#define HCI_CMD_Exit_Sniff_Mode 0x0004 /* status */
struct hciExitSniffMode {
	uint16_t conn;
} __packed;

#define HCI_CMD_Park_State 0x0005 /* status */
struct hciParkState {
	uint16_t conn;
	uint16_t beaconMaxInt;
	uint16_t beaconMinInt;
} __packed;

#define HCI_CMD_Exit_Park_State 0x0006 /* status */
struct hciExitParkState {
	uint16_t conn;
} __packed;

#define HCI_CMD_QoS_Setup 0x0007 /* status */
struct hisQosSetup {
	uint16_t conn;
	uint8_t flags;
	uint8_t serviceType;
	uint32_t tokenRate;
	uint32_t peakBandwidth;
	uint32_t latency;
	uint32_t delayVariation;
} __packed;

#define HCI_CMD_Role_Discovery 0x0009 /* complete */
struct hciRoleDiscovery {
	uint16_t conn;
} __packed;
struct hciCmplRoleDiscovery {
	uint8_t status;
} __packed;

#define HCI_CMD_Switch_Role 0x000B /* status */
struct hciSwitchRole {
	uint8_t mac[6];
	uint8_t becomeSlave;
} __packed;

#define HCI_CMD_Read_Link_Policy_Settings 0x000C /* complete */
struct hciReadLinkPolicySettings {
	uint16_t conn;
} __packed;
struct hciCmplReadLinkPolicySettings {
	uint8_t status;
	uint16_t conn;
	uint16_t policy; /* HCI_LINK_POLICY_* */
} __packed;

#define HCI_CMD_Write_Link_Policy_Settings 0x000D /* complete */
struct hciWriteLinkPolicySettings {
	uint16_t conn;
	uint16_t policy; /* HCI_LINK_POLICY_* */
} __packed;
struct hciCmplWriteLinkPolicySettings {
	uint8_t status;
	uint16_t conn;
} __packed;

/* ==== BT 1.2 ==== */

#define HCI_CMD_Read_Default_Link_Policy_Settings 0x000E /* complete */
struct hciCmplReadDefaultLinkPolicySettings {
	uint8_t status;
	uint16_t policy; /* HCI_LINK_POLICY_* */
} __packed;

#define HCI_CMD_Write_Default_Link_Policy_Settings 0x000F /* complete */
struct hciWriteDefaultLinkPolicySettings {
	uint16_t policy; /* HCI_LINK_POLICY_* */
} __packed;
struct hciCmplWriteDefaultLinkPolicySettings {
	uint8_t status;
} __packed;

#define HCI_CMD_Flow_Specification 0x0010 /* status */
struct hisFlowSpecification {
	uint16_t conn;
	uint8_t flags;
	uint8_t flowDirection;
	uint8_t serviceType;
	uint32_t tokenRate;
	uint32_t tockenBucketSize;
	uint32_t peakBandwidth;
	uint32_t accessLatency;
} __packed;

/* ==== BT 2.1 ==== */

#define HCI_CMD_Sniff_Subrating 0x0011 /* complete */
struct hciSniffSubrating {
	uint16_t conn;
	uint16_t maxLatency;
	uint16_t minRemoteTimeout;
	uint16_t minLocalTimeout;
} __packed;
struct hciCmplSniffSubrating {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_OGF_Controller_and_Baseband 3

/* ==== BT 1.1 ==== */

#define HCI_CMD_Set_Event_Mask 0x0001 /* complete */
struct hciSetEventMask {
	uint64_t mask; /* bitmask of HCI_EVENT_* */
} __packed;
struct hciCmplSetEventMask {
	uint8_t status;
} __packed;

#define HCI_CMD_Reset 0x0003 /* complete */
struct hciCmplReset {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_Event_Filter 0x0005 /* complete */
struct hciSetEventFilter {
	uint8_t filterType; /* HCI_FILTER_TYPE_* */
	/* more things are optional here */
} __packed;
struct hciCmplSetEventFiler {
	uint8_t status;
} __packed;

#define HCI_CMD_Flush 0x0008 /* complete */
struct hciFlush {
	uint16_t conn;
} __packed;
struct hciCmplFlush {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_PIN_Type 0x0009 /* complete */
struct hciCmplReadPinType {
	uint8_t status;
	uint8_t isFixed;
} __packed;

#define HCI_CMD_Write_PIN_Type 0x000A /* complete */
struct hciWritePinType {
	uint8_t isFixed;
} __packed;
struct hciCmplWritePinType {
	uint8_t status;
} __packed;

#define HCI_CMD_Create_New_Unit_Key 0x000B /* complete */
struct hciCmplCreateNewUnitKey {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Stored_Link_Key 0x000D /* complete */
struct hciReadStoredLinkKey {
	uint8_t mac[6];
	uint8_t readAll;
} __packed;
struct hciCmplReadStoredLinkKey {
	uint8_t status;
	uint16_t maxNumKeys;
	uint16_t numKeysRead;
} __packed;

#define HCI_CMD_Write_Stored_Link_Key 0x0011 /* complete */
struct hciWriteStoredLinkKeyItem {
	uint8_t mac[6];
	uint8_t key[16];
} __packed;
struct hciWriteStoredLinkKey {
	uint8_t numKeys;
	struct hciWriteStoredLinkKeyItem items[];
} __packed;
struct hciCmplWriteStoredLinkKey {
	uint8_t status;
	uint8_t numKeysWritten;
} __packed;

#define HCI_CMD_Delete_Stored_Link_Key 0x0012 /* complete */
struct hciDeleteStoredLinkKey {
	uint8_t mac[6];
	uint8_t deleteAll;
} __packed;
struct hciCmplDeleteStoredLinkKey {
	uint8_t status;
	uint8_t numKeysDeleted;
} __packed;

#define HCI_CMD_Write_Local_Name 0x0013 /* complete */
struct hciWriteLocalName {
	char name[HCI_DEV_NAME_LEN];
} __packed;
struct hciCmplWriteLocalName {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Local_Name 0x0014 /* complete */
struct hciCmplReadLocalName {
	uint8_t status;
	char name[HCI_DEV_NAME_LEN];
} __packed;

#define HCI_CMD_Read_Connection_Accept_Timeout 0x0015 /* complete */
struct hciCmplReadConnAcceptTimeout {
	uint8_t status;
	uint16_t timeout; /* in units of 0.625ms 1..0xB540 */
} __packed;

#define HCI_CMD_Write_Connection_Accept_Timeout 0x0016 /* complete */
struct hciWriteConnAcceptTimeout {
	uint16_t timeout; /* in units of 0.625ms 1..0xB540 */
} __packed;
struct hciCmplWriteConnAcceptTimeout {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Page_Timeout 0x0017 /* complete */
struct hciCmplReadPageTimeout {
	uint8_t status;
	uint16_t timeout;
} __packed;

#define HCI_CMD_Write_Page_Timeout 0x0018 /* complete */
struct hciWritePageTimeout {
	uint16_t timeout;
} __packed;
struct hciCmplWritePageTimeout {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Scan_Enable 0x0019 /* complete */
struct hciCmplReadScanEnable {
	uint8_t status;
	uint8_t state; /* bitmask of HCI_SCAN_ENABLE_* */
} __packed;

#define HCI_CMD_Write_Scan_Enable 0x001A /* complete */
struct hciWriteScanEnable {
	uint8_t state; /* bitmask of HCI_SCAN_ENABLE_* */
} __packed;
struct hciCmplWriteScanEnable {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Page_Scan_Activity 0x001B /* complete */
struct hciCmplReadPageScanActivity {
	uint8_t status;
	uint16_t scanInterval;
	uint16_t scanWindow;
} __packed;

#define HCI_CMD_Write_Page_Scan_Activity 0x001C /* complete */
struct hciWritePageScanActivity {
	uint16_t scanInterval;
	uint16_t scanWindow;
} __packed;
struct hciCmplWritePageScanActivity {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Inquiry_Scan_Activity 0x001D /* complete */
struct hciCmplReadInquiryScanActivity {
	uint8_t status;
	uint16_t scanInterval;
	uint16_t scanWindow;
} __packed;

#define HCI_CMD_Write_Inquiry_Scan_Activity 0x001E /* complete */
struct hciWriteInquiryScanActivity {
	uint16_t scanInterval;
	uint16_t scanWindow;
} __packed;
struct hciCmplWriteInquiryScanActivity {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Authentication_Enable 0x001F /* complete */
struct hciCmplReadAuthEnable {
	uint8_t status;
	uint8_t authRequired;
} __packed;

#define HCI_CMD_Write_Authentication_Enable 0x0020 /* complete */
struct hciWriteAuthEnable {
	uint8_t authRequired;
} __packed;
struct hciCmplWriteAuthEnable {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Encryption_Mode \
	0x0021 /* complete */ /* deprecated in BT 2.1+ */
struct hciCmplReadEncryptionMode {
	uint8_t status;
	uint8_t encrRequired;
} __packed;

#define HCI_CMD_Write_Encryption_Mode \
	0x0022 /* complete */ /* deprecated in BT 2.1+ */
struct hciWriteEncryptionMode {
	uint8_t encrRequired;
} __packed;
struct hciCmplWriteEncryptionMode {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Class_Of_Device 0x0023 /* complete */
struct hciCmplReadClassOfDevice {
	uint8_t status;
	uint8_t cls[3];
} __packed;

#define HCI_CMD_Write_Class_Of_Device 0x0024 /* complete */
struct hciWriteClassOfDevice {
	uint8_t cls[3];
} __packed;
struct hciCmplWriteClassOfDevice {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Voice_Setting 0x0025 /* complete */
struct hciCmplReadVoiceSetting {
	uint8_t status;
	uint16_t voiceSetting;
} __packed;

#define HCI_CMD_Write_Voice_Setting 0x0026 /* complete */
struct hciWriteVoiceSetting {
	uint16_t voiceSetting;
} __packed;
struct hciCmplWriteVoiceSetting {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Automatic_Flush_Timeout 0x0027 /* complete */
struct hciReadAutoFlushTimeout {
	uint16_t conn;
} __packed;
struct hciCmplReadAutoFlushTimeout {
	uint8_t status;
	uint16_t conn;
	uint16_t timeout;
} __packed;

#define HCI_CMD_Write_Automatic_Flush_Timeout 0x0028 /* complete */
struct hciWriteAutoFlushTimeout {
	uint16_t conn;
	uint16_t timeout;
} __packed;
struct hciCmplWriteAutoFlushTimeout {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Num_Broadcast_Retransmissions 0x0029 /* complete */
struct hciCmplReadNumBroadcastRetransmissions {
	uint8_t status;
	uint8_t numRetransmissions; /* 0 .. 0xFE => 1 .. 255 TXes */
} __packed;

#define HCI_CMD_Write_Num_Broadcast_Retransmissions 0x002A /* complete */
struct hciWriteNumBroadcastRetransmissions {
	uint8_t numRetransmissions; /* 0 .. 0xFE => 1 .. 255 TXes */
} __packed;
struct hciCmplWriteNumBroadcastRetransmissions {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Hold_Mode_Activity 0x002B /* complete */
struct hciCmplReadHoldModeActivity {
	uint8_t status;
	uint8_t holdModeActivity; /* bitfield if HCI_HOLD_MODE_SUSPEND_* */
} __packed;

#define HCI_CMD_Write_Hold_Mode_Activity 0x002C /* complete */
struct hciWriteHoldModeActivity {
	uint8_t holdModeActivity; /* bitfield if HCI_HOLD_MODE_SUSPEND_* */
} __packed;
struct hciCmplWriteHoldModeActivity {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Transmit_Power_Level 0x002D /* complete */
struct hciReadTransmitPowerLevel {
	uint16_t conn;
	uint8_t max; /* else current */
} __packed;
struct hciCmplReadTransmitPowerLevel {
	uint8_t status;
	uint16_t conn;
	uint8_t txPower; /* actually an int8_t */
} __packed;

#define HCI_CMD_Read_SCO_Flow_Control_Enable 0x002E /* complete */
struct hciCmplReadSyncFlowCtrl {
	uint8_t status;
	uint8_t syncFlowCtrlOn;
} __packed;

#define HCI_CMD_Write_SCO_Flow_Control_Enable 0x002F /* complete */
struct hciWriteSyncFlowCtrlEnable {
	uint8_t syncFlowCtrlOn;
} __packed;
struct hciCmplWriteSyncFlowCtrlEnable {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_Controller_To_Host_Flow_Control 0x0031 /* complete */
struct hciSetControllerToHostFlowControl {
	uint8_t chipToHostFlowCtrl; /* bitmask of HCI_TO_HOST_FLOW_CTRL_* */
} __packed;
struct hciCmplSetControllerToHostFlowControl {
	uint8_t status;
} __packed;

#define HCI_CMD_Host_Buffer_Size 0x0033 /* complete */
struct hciHostBufferSize {
	uint16_t maxAclPacket;
	uint8_t maxScoPacket;
	uint16_t numAclPackets;
	uint16_t numScoPackets;
} __packed;
struct hciCmplHostBufferSize {
	uint8_t status;
} __packed;

#define HCI_CMD_Host_Number_Of_Completed_Packets                               \
	0x0035 /* special: can be sent anytime (not subj to cmd flow control), \
		  does not generate events unless error */
struct hciHostNumberOfCompletedPacketsItem {
	uint16_t conn;
	uint16_t numCompletedPackets;
} __packed;
struct hciHostNumberOfCompletedPackets {
	uint8_t numHandles;
	struct hciHostNumberOfCompletedPacketsItem items[];
} __packed;

#define HCI_CMD_Read_Link_Supervision_Timeout 0x0036 /* complete */
struct hciReadLinkSupervisionTimeout {
	uint16_t conn;
} __packed;
struct hciCmplReadLinkSupervisionTimeout {
	uint8_t status;
	uint16_t conn;
	uint16_t timeout; /* in units of 0.625ms allowed: 1..0xffff, required
			     support 0x0190 - 0xffff */
} __packed;

#define HCI_CMD_Write_Link_Supervision_Timeout 0x0037 /* complete */
struct hciWriteLinkSupervisionTimeout {
	uint16_t conn;
	uint16_t timeout; /* in units of 0.625ms allowed: 1..0xffff, required
			     support 0x0190 - 0xffff */
} __packed;
struct hciCmplWriteLinkSupervisionTimeout {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Number_Of_Supported_IAC 0x0038 /* complete */
struct hciCmplReadNumberOfSupportedIac {
	uint8_t status;
	uint8_t numSupportedIac;
} __packed;

#define HCI_CMD_Read_Current_IAC_LAP 0x0039 /* complete */
struct hciCmplReadCurrentIacItem {
	uint8_t iac_lap[3];
} __packed;
struct hciCmplReadCurrentIac {
	uint8_t status;
	uint8_t numCurrentIac;
	struct hciCmplReadCurrentIacItem items[];
} __packed;

#define HCI_CMD_Write_Current_IAC_LAP 0x003A /* complete */
struct hciWriteCurrentIacLapItem {
	uint8_t iacLap[3];
} __packed;
struct hciWriteCurrentIacLap {
	uint8_t numCurrentIac;
	struct hciWriteCurrentIacLapItem items[];
} __packed;
struct hciCmplWriteCurrentIacLap {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Page_Scan_Period_Mode 0x003B /* complete */
struct hciCmplReadPageScanPeriodMode {
	uint8_t status;
	uint8_t mode;
} __packed;

#define HCI_CMD_Write_Page_Scan_Period_Mode 0x003C /* complete */
struct hciWritePageScanPeriodMode {
	uint8_t mode;
} __packed;
struct hciCmplWritePageScanPeriodMode {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Page_Scan_Mode \
	0x003D /* complete */ /* deprecated in BT 1.2+ */
struct hciCmplReadPageScanMode {
	uint8_t status;
	uint8_t pageScanMode; /* nonzero modes are optional */
} __packed;

#define HCI_CMD_Write_Page_Scan_Mode \
	0x003E /* complete */ /* deprecated in BT 1.2+ */
struct hciWritePageScanMode {
	uint8_t pageScanMode; /* nonzero modes are optional */
} __packed;
struct hciCmplWritePageScanMode {
	uint8_t status;
} __packed;

/* ==== BT 1.2 ==== */

#define HCI_CMD_Set_AFH_Host_Channel_Classification 0x003F /* complete */
struct hciSetAfhHostChannelClassification {
	uint8_t channels[10];
} __packed;
struct hciCmplSetAfhHostChannelClassification {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Inquiry_Scan_Type 0x0042 /* complete */
struct hciCmplReadInquiryScanType {
	uint8_t status;
	uint8_t interlaced; /* optional */
} __packed;

#define HCI_CMD_Write_Inquiry_Scan_Type 0x0043 /* complete */
struct hciWriteInquiryScanType {
	uint8_t interlaced; /* optional */
} __packed;
struct hciCmplWriteInquiryScanType {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Inquiry_Mode 0x0044 /* complete */
struct hciCmplReadInquryMode {
	uint8_t status;
	uint8_t inqMode; /* HCI_INQ_MODE_* */
} __packed;

#define HCI_CMD_Write_Inquiry_Mode 0x0045 /* complete */
struct hciWriteInquiryMode {
	uint8_t inqMode; /* HCI_INQ_MODE_* */
} __packed;
struct hciCmplWriteInquiryMode {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Page_Scan_Type 0x0046 /* complete */
struct hciCmplReadPageScanType {
	uint8_t status;
	uint8_t interlaced; /* optional */
} __packed;

#define HCI_CMD_Write_Page_Scan_Type 0x0047 /* complete */
struct hciWritePageScanType {
	uint8_t interlaced; /* optional */
} __packed;
struct hciCmplWritePageScanType {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_AFH_Channel_Assessment_Mode 0x0048 /* complete */
struct hciCmplReadAfhChannelAssessment {
	uint8_t status;
	uint8_t channelAssessmentEnabled;
} __packed;

#define HCI_CMD_Write_AFH_Channel_Assessment_Mode 0x0049 /* complete */
struct hciWriteAfhChannelAssessment {
	uint8_t channelAssessmentEnabled;
} __packed;
struct hciCmplWriteAfhChannelAssessment {
	uint8_t status;
} __packed;

/* ==== BT 2.1 ==== */

#define HCI_CMD_Read_Extended_Inquiry_Response 0x0051 /* complete */
struct hciCmplReadEIR {
	uint8_t status;
	uint8_t useFec;
	uint8_t data[240];
} __packed;

#define HCI_CMD_Write_Extended_Inquiry_Response 0x0052 /* complete */
struct hciWriteEIR {
	uint8_t useFec;
	uint8_t data[240];
} __packed;
struct hciCmplWriteEIR {
	uint8_t status;
} __packed;

#define HCI_CMD_Refresh_Encryption_Key 0x0052 /* status */
struct hciRefreshEncryptionKey {
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Simple_Pairing_Mode 0x0055 /* complete */
struct hciCmplReadSimplePairingMore {
	uint8_t status;
	uint8_t useSsp;
} __packed;

#define HCI_CMD_Write_Simple_Pairing_Mode 0x0056 /* complete */
struct hciWriteSimplePairingMode {
	uint8_t useSsp;
} __packed;
struct hciCmplWriteSimplePairingMode {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Local_OOB_Data 0x0057 /* complete */
struct hciCmplReadLocalOobData {
	uint8_t status;
	uint8_t C[16];
	uint8_t R[16];
} __packed;

#define HCI_CMD_Read_Inquiry_Response_Transmit_Power_Level \
	0x0058 /* complete                                 \
		*/
struct hciCmplReadInquiryTransmitPowerLevel {
	uint8_t status;
	uint8_t power; /* actually an int8_t */
} __packed;

#define HCI_CMD_Write_Inquiry_Transmit_Power_Level 0x0059 /* complete */
struct hciWriteInquiryTransmitPowerLevel {
	uint8_t power; /* actually an int8_t */
} __packed;
struct hciCmplWriteInquiryTransmitPowerLevel {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Default_Erroneous_Data_Reporting 0x005A /* complete */
struct hciCmplReadErroneousDataReporting {
	uint8_t status;
	uint8_t reportingEnabled;
} __packed;

#define HCI_CMD_Write_Default_Erroneous_Data_Reporting 0x005B /* complete */
struct hciWriteErroneousDataReporting {
	uint8_t reportingEnabled;
} __packed;
struct hciCmplWriteErroneousDataReporting {
	uint8_t status;
} __packed;

#define HCI_CMD_Enhanced_Flush 0x005F /* status */
struct hciEnhancedFlush {
	uint16_t conn;
	uint8_t which; /* 0 is the only value - flush auto-flushable packets
			  only */
} __packed;

#define HCI_CMD_Send_Keypress_Notification 0x0060 /* complete */
struct hciSendKeypressNotification {
	uint8_t mac[6];
	uint8_t notifType; /* HCI_SSP_KEY_ENTRY_* */
} __packed;
struct hciCmplSendKeypressNotification {
	uint8_t status;
	uint8_t mac[6];
} __packed;

/* ==== BT 3.0 ==== */

#define HCI_CMD_Read_Logical_Link_Accept_Timeout 0x0061 /* complete */
struct hciCmplReadLogicalLinkTimeout {
	uint8_t status;
	uint16_t timeout; /* in units of 0.625ms 1..0xB540. Required support
			     0x00A0..0xB540 */
} __packed;

#define HCI_CMD_Write_Logical_Link_Accept_Timeout 0x0062 /* complete */
struct hciWriteLogicalLinkTimeout {
	uint16_t timeout; /* in units of 0.625ms 1..0xB540. Required support
			     0x00A0..0xB540 */
} __packed;
struct hciCmplWriteLogicalLinkTimeout {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_Event_Mask_Page_2 0x0063 /* complete */
struct hciSetEventMaskPage2 {
	uint64_t mask; /* bitmask of HCI_EVENT_P2_* */
} __packed;
struct hciCmplSetEventMaskPage2 {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Location_Data 0x0064 /* complete */
struct hciCmplReadLocationData {
	uint8_t status;
	uint8_t regulatoryDomainKnown;
	uint16_t domain; /* ISO3166-1 code if known, else 0x5858 'XX' */
	uint8_t locationSuffix; /* HCI_LOCATION_DOMAIN_OPTION_* */
	uint8_t mainsPowered;
} __packed;

#define HCI_CMD_Write_Location_Data 0x0065 /* complete */
struct hciWriteLocationData {
	uint8_t regulatoryDomainKnown;
	uint16_t domain; /* ISO3166-1 code if known, else 0x5858 'XX' */
	uint8_t locationSuffix; /* HCI_LOCATION_DOMAIN_OPTION_* */
	uint8_t mainsPowered;
} __packed;
struct hciCmplWriteLocationData {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Flow_Control_Mode 0x0066 /* complete */
struct hciCmplReadFlowControlMode {
	uint8_t status;
	uint8_t blockBased; /* block based is for amp, packed-based is for
			       BR/EDR */
} __packed;

#define HCI_CMD_Write_Flow_Control_mode 0x0067 /* complete */
struct hciWriteFlowControlMode {
	uint8_t blockBased; /* block based is for amp, packed-based is for
			       BR/EDR */
} __packed;
struct hciCmplWriteFlowcontrolMode {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Enhanced_Transmit_Power_Level 0x0068 /* complete */
struct hciReadEnhancedTransmitPowerLevel {
	uint16_t conn;
	uint8_t max; /* else currurent is read */
} __packed;
struct hciCmplReadEnhancedTransmitPowerLevel {
	uint8_t status;
	uint16_t conn;
	uint8_t txLevelGFSK; /* actually an int8_t */
	uint8_t txLevelDQPSK; /* actually an int8_t */
	uint8_t txLevel8DPSK; /* actually an int8_t */
} __packed;

#define HCI_CMD_Read_Best_Effort_Flush_Timeout 0x0069 /* complete */
struct hciReadBestEffortFlushTimeout {
	uint16_t logicalLinkHandle;
} __packed;
struct hciCmplReadBestEffortFlushTimeout {
	uint8_t status;
	uint32_t bestEffortFlushTimeout; /* in microseconds */
} __packed;

#define HCI_CMD_Write_Best_Effort_Flush_Timeout 0x006A /* complete */
struct hciWriteBestEffortFlushTimeout {
	uint16_t logicalLinkHandle;
	uint32_t bestEffortFlushTimeout; /* in microseconds */
} __packed;
struct hciCmplWriteBestEffortFlushTimeout {
	uint8_t status;
} __packed;

#define HCI_CMD_Short_Range_Mode 0x006B /* status */
struct hciShortRangeMode {
	uint8_t physicalLinkHandle;
	uint8_t shortRangeModeEnabled;
} __packed;

/* ==== BT 4.0 ==== */

#define HCI_CMD_Read_LE_Host_Supported 0x006C /* complete */
struct hciCmplReadLeHostSupported {
	uint8_t status;
	uint8_t leSupportedHost;
	uint8_t simultaneousLeHost;
} __packed;

#define HCI_CMD_Write_LE_Host_Supported 0x006D /* complete */
struct hciWriteLeHostSupported {
	uint8_t leSupportedHost;
	uint8_t simultaneousLeHost;
} __packed;
struct hciCmplWriteLeHostSupported {
	uint8_t status;
} __packed;

/* ==== BT 4.1 ==== */

#define HCI_CMD_Set_MWS_Channel_Parameters 0x006E /* complete */
struct hciSetMwsChannelParams {
	uint8_t mwsEnabled;
	uint16_t mwsChannelRxCenterFreq; /* in MHz */
	uint16_t mwsChannelTxCenterFreq; /* in MHz */
	uint16_t mwsChannelRxBandwidth; /* in MHz */
	uint16_t mwsChannelTxBandwidth; /* in MHz */
	uint8_t mwsChannelType;
} __packed;
struct hciCmplSetMwsChannelParams {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_External_Frame_Configuration 0x006F /* complete */
struct hciSetExternalFrameConfigItem {
	uint16_t periodDuration; /* in microseconds */
	uint8_t periodType; /* HCI_PERIOD_TYPE_* */
} __packed;
struct hciSetExternalFrameConfig {
	uint16_t extFrameDuration; /* in microseonds */
	uint16_t extFrameSyncAssertOffset; /* in microseonds */
	uint16_t extFrameSyncAssertJitter; /* in microseonds */
	uint8_t extNumPeriods; /* 1 .. 32 */
	struct hciSetExternalFrameConfigItem items[];
} __packed;
struct hciCmplSetExternalFrameConfig {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_MWS_Signalling 0x0070 /* complete */
struct hciSetMwsSignalling {
	uint16_t mwsRxAssertOffset; /* all of these are in microseconds */
	uint16_t mwsRxAssertJitter;
	uint16_t mwsRxDeassertOffset;
	uint16_t mwsRxDeassertJitter;
	uint16_t mwsTxAssertOffset;
	uint16_t mwsTxAssertJitter;
	uint16_t mwsTxDeassertOffset;
	uint16_t mwsTxDeassertJitter;
	uint16_t mwsPatternAssertOffset;
	uint16_t mwsPatternAssertJitter;
	uint16_t mwsInactivityDurationAssertOffset;
	uint16_t mwsInactivityDurationAssertJitter;
	uint16_t mwsScanFrequencyAssertOffset;
	uint16_t mwsScanFrequencyAssertJitter;
	uint16_t mwsPriorityAssertOffsetRequest;
} __packed;
struct hciCmplSetMwsSignalling {
	uint8_t status;
	uint16_t bluetoothRxPriorityAssertOffset;
	uint16_t bluetoothRxPriorityAssertJitter;
	uint16_t bluetoothRxPriorityDeassertOffset;
	uint16_t bluetoothRxPriorityDeassertJitter;
	uint16_t _802RxPriorityAssertOffset;
	uint16_t _802RxPriorityAssertJitter;
	uint16_t _802RxPriorityDeassertOffset;
	uint16_t _802RxPriorityDeassertJitter;
	uint16_t bluetoothTxOnAssertOffset;
	uint16_t bluetoothTxOnAssertJitter;
	uint16_t bluetoothTxOnDeassertOffset;
	uint16_t bluetoothTxOnDeassertJitter;
	uint16_t _802TxOnAssertOffset;
	uint16_t _802TxOnAssertJitter;
	uint16_t _802TxOnDeassertOffset;
	uint16_t _802TxOnDeassertJitter;
} __packed;

#define HCI_CMD_Set_MWS_Transport_Layer 0x0071 /* complete */
struct hciSetMwsTransportLayer {
	uint8_t transportLayer;
	uint32_t toMwsBaudRate; /* in byte/sec */
	uint32_t fromMwsBaudRate; /* in byte/sec */
} __packed;
struct hciCmplSetMwsTransportLayer {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_MWS_Scan_Frequency_Table 0x0072 /* complete */
struct hciSetMwsScanFrequencyTableItem {
	uint16_t scanFreqLow; /*in MHz */
	uint16_t scanFreqHigh; /*in MHz */
} __packed;
struct hciSetMwsScanFrequencyTable {
	uint8_t n;
	struct hciSetMwsScanFrequencyTableItem items[];
} __packed;
struct hciCmplSetMwsScanFrequencyTable {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_MWS_PATTERN_Configuration 0x0073 /* complete */
struct hciSetMwsPatternConfigItem {
	uint16_t intervalDuration; /* in microseconds */
	uint8_t intervalType; /* HCI_MWS_INTERVAL_TYPE_* */
} __packed;
struct hciSetMwsPatternConfig {
	uint8_t mwsPatternIndex; /* 0 .. 2 */
	uint8_t mwsPatternNumIntervals;
	struct hciSetMwsPatternConfigItem items[];
} __packed;
struct hciCmplSetMwsPatternConfig {
	uint8_t status;
} __packed;

#define HCI_CMD_Set_Reserved_LT_ADDR 0x0074 /* complete */
struct hciSetReservedLtAddr {
	uint8_t ltAddr;
} __packed;
struct hciCmplSetReservedLtAddr {
	uint8_t status;
	uint8_t ltAddr;
} __packed;

#define HCI_CMD_Delete_Reserved_LT_ADDR 0x0075 /* complete */
struct hciDeleteReservedLtAddr {
	uint8_t ltAddr;
} __packed;
struct hciCmplDeleteReservedLtAddr {
	uint8_t status;
	uint8_t ltAddr;
} __packed;

#define HCI_CMD_Set_Connectionless_Slave_Broadcast_Data 0x0076 /* complete */
struct hciSetConnlessSlaveBroadcastData {
	uint8_t ltAddr;
	uint8_t fragment; /* HCI_CONNLESS_FRAG_TYPE_* */
	uint8_t dataLen;
	uint8_t data[];
} __packed;
struct hciCmplSetConnlessSlaveBroadcastData {
	uint8_t status;
	uint8_t ltAddr;
} __packed;

#define HCI_CMD_Read_Synchronisation_Train_Parameters 0x0077 /* complete */
struct hciCmplReadSyncTrainParams {
	uint8_t status;
	uint16_t interval;
	uint32_t syncTrainTimeout;
	uint8_t serviceData;
} __packed;

#define HCI_CMD_Write_Synchronisation_Train_Parameters 0x0078 /* complete */
struct hciWriteSyncTrainParams {
	uint16_t intMin;
	uint16_t intMax;
	uint32_t syncTrainTimeout;
	uint8_t serviceData;
} __packed;
struct hciCmplWriteSyncTrainParams {
	uint8_t status;
	uint16_t interval;
} __packed;

#define HCI_CMD_Read_Secure_Connections_Host_Support 0x0079 /* complete */
struct hciCmplReadSecureConnectionsHostSupport {
	uint8_t status;
	uint8_t secureConnectionsSupported;
} __packed;

#define HCI_CMD_Write_Secure_Connections_Host_Support 0x007A /* complete */
struct hciWriteSecureConnectionsHostSupport {
	uint8_t secureConnectionsSupported;
} __packed;
struct hciCmplWriteSecureConnectionsHostSupport {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Authenticated_Payload_Timeout 0x007B /* complete */
struct hciReadAuthedPayloadTimeout {
	uint16_t conn;
} __packed;
struct hciCmplReadAuthedPayloadTimeout {
	uint8_t status;
	uint16_t conn;
	uint16_t timeout; /* in units of 10ms, 1 .. 0xffff */
} __packed;

#define HCI_CMD_Write_Authenticated_Payload_Timeout 0x007C /* complete */
struct hciWriteAuthedPayloadTimeout {
	uint16_t conn;
	uint16_t timeout; /* in units of 10ms, 1 .. 0xffff */
} __packed;
struct hciCmplWriteAuthedPayloadTimeout {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Local_OOB_Extended_Data 0x007D /* complete */
struct hciCmplReadLocalOobExtendedData {
	uint8_t status;
	uint8_t C_192[16];
	uint8_t R_192[16];
	uint8_t C_256[16];
	uint8_t R_256[16];
} __packed;

#define HCI_CMD_Read_Extended_Page_Timeout 0x007E /* complete */
struct hciCmplReadExtendedPageTimeout {
	uint8_t status;
	uint16_t timeout; /* in units of 0.625ms 0..0xffff */
} __packed;

#define HCI_CMD_Write_Extended_Page_Timeout 0x007F /* complete */
struct hciWriteExtendedPageTimeout {
	uint16_t timeout; /* in units of 0.625ms 0..0xffff */
} __packed;
struct hciCmplWriteExtendedPageTimeout {
	uint8_t status;
} __packed;

#define HCI_CMD_Read_Extended_Inquiry_Length 0x0080 /* complete */
struct hciCmplReadExtendedInquiryLength {
	uint8_t status;
	uint16_t timeout; /* in units of 0.625ms 0..0xffff */
} __packed;

#define HCI_CMD_Write_Extended_Inquiry_Length 0x0081 /* complete */
struct hciWriteExtendedInquiryLength {
	uint16_t timeout; /* in units of 0.625ms 0..0xffff */
} __packed;
struct hciCmplWriteExtendedInquiryLength {
	uint8_t status;
} __packed;

#define HCI_OGF_Informational 4

/* ==== BT 1.1 ==== */

#define HCI_CMD_Read_Local_Version_Information 0x0001 /* complete */
struct hciCmplReadLocalVersion {
	uint8_t status;
	uint8_t hciVersion; /* HCI_VERSION_* */
	uint16_t hciRevision;
	uint8_t lmpVersion; /* HCI_VERSION_* */
	uint16_t manufName;
	uint16_t lmpSubversion;
} __packed;

#define HCI_CMD_Read_Local_Supported_Commands 0x0002 /* complete */
struct hciCmplReadLocalSupportedCommands {
	uint8_t status;
	uint64_t bitfield;
} __packed;

#define HCI_CMD_Read_Local_Supported_Features 0x0003 /* complete */
struct hciCmplReadLocalSupportedFeatures {
	uint8_t status;
	uint64_t features; /* bitmask of HCI_LMP_FTR_* */
} __packed;

#define HCI_CMD_Read_Local_Extended_Features 0x0004 /* complete */
struct hciReadLocalExtendedFeatures {
	uint8_t page;
} __packed;
struct hciCmplReadLocalExtendedFeatures {
	uint8_t status;
	uint8_t page;
	uint8_t maxPage;
	uint64_t features; /* bitmask of HCI_LMP_EXT_FTR_P* */
} __packed;

#define HCI_CMD_Read_Buffer_Size 0x0005 /* complete */
struct hciCmplReadBufferSize {
	uint8_t status;
	uint16_t aclBufferLen;
	uint8_t scoBufferLen;
	uint16_t numAclBuffers;
	uint16_t numScoBuffers;
} __packed;

#define HCI_CMD_Read_BD_ADDR 0x0009 /* complete */
struct hciCmplReadBdAddr {
	uint8_t status;
	uint8_t mac[6];
} __packed;

/* ==== BT 3.0 ==== */

#define HCI_CMD_Read_Data_Block_Size 0x000A /* complete */
struct hciCmplReadDataBlockSize {
	uint8_t status;
	uint16_t maxAclDataPacketLen;
	uint16_t dataBlockLen;
	uint16_t totalNumDataBlocks;
} __packed;

/* ==== BT 4.1 ==== */

#define HCI_CMD_Read_Local_Supported_Codecs 0x000B /* complete */
struct hciCmplReadLocalSupportedCodecs {
	uint8_t status;
	uint8_t numSupportedCodecs;
	uint8_t codecs[];
	/* these follow, but due to var array cannot be declared here:
		uint8_t numVendorCodecs;
		uint32_t vendorCodecs[];
	*/
} __packed;

#define HCI_OGF_Status 5

/* == BT 1.1 == */

#define HCI_CMD_Read_Failed_Contact_Counter 0x0001 /* complete */
struct hciReadFailedContactCounter {
	uint16_t conn;
} __packed;
struct hciCmplReadFailedContactCounter {
	uint8_t status;
	uint16_t conn;
	uint16_t counter;
} __packed;

#define HCI_CMD_Reset_Failed_Contact_Counter 0x0002 /* complete */
struct hciResetFailedContactCounter {
	uint16_t conn;
} __packed;
struct hciCmplResetFailedContactCounter {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_Read_Link_Quality 0x0003 /* complete */
struct hciReadLinkQuality {
	uint16_t conn;
} __packed;
struct hciCmplReadLinkQuality {
	uint8_t status;
	uint16_t conn;
	uint8_t quality;
} __packed;

#define HCI_CMD_Read_RSSI 0x0005 /* complete */
struct hciReadRssi {
	uint16_t conn;
} __packed;
struct hciCmplReadRssi {
	uint8_t status;
	uint16_t conn;
	uint8_t RSSI; /* actually an int8_t */
} __packed;

/* ==== BT 1.2 ==== */

#define HCI_CMD_Read_AFH_Channel_Map 0x0006 /* complete */
struct hciReadAfhChannelMap {
	uint16_t conn;
} __packed;
struct hciCmplReadAfhChannelMap {
	uint8_t status;
	uint16_t conn;
	uint8_t map[10];
} __packed;

#define HCI_CMD_Read_Clock 0x0007 /* complete */
struct hciReadClock {
	uint16_t conn;
	uint8_t readRemote; /* else reads local and ignores conn */
} __packed;
struct hciCmplReadClock {
	uint8_t status;
	uint16_t conn;
	uint32_t clock;
	uint16_t accuracy;
} __packed;

/* ==== BT 3.0 ==== */

#define HCI_CMD_Read_Encryption_Key_Size 0x0008 /* complete */
struct hciReadEncrKeySize {
	uint16_t conn;
} __packed;
struct hciCmplReadEncrKeySize {
	uint8_t status;
	uint16_t conn;
	uint8_t keySize;
} __packed;

#define HCI_CMD_Read_Local_AMP_Info 0x0009 /* complete */
struct hciCmplReadLocalAmpInfo {
	uint8_t status;
	uint8_t ampStatus;
	uint32_t totalBandwidth;
	uint32_t maxGuaranteedBandwidth;
	uint32_t minLatency;
	uint16_t maxPduSize;
	uint8_t controllerType;
	uint16_t palCapabilities;
	uint16_t maxAmpAssocLen;
	uint32_t maxFlushTimeout;
	uint32_t bestEffortFlushTimeout;
} __packed;

#define HCI_CMD_Read_Local_AMP_ASSOC 0x000A /* complete */
struct hciReadLocalAmpAssoc {
	uint8_t physicalLinkHandle;
	uint16_t lengthSoFar;
	uint16_t ampAssocLen;
} __packed;
struct hciCmplReadLocalAmpAssoc {
	uint8_t status;
	uint8_t physicalLinkHandle;
	uint16_t ampAssocRemainingLen; /* incl this fragment */
	uint8_t ampAssocFragment[]; /* 1.. 248 byutes */
} __packed;

#define HCI_CMD_Write_Remote_AMP_ASSOC 0x000B /* complete */
struct hciWriteRemoteAmpAssoc {
	uint8_t physicalLinkHandle;
	uint16_t lengthSoFar;
	uint16_t remaningLength;
	uint8_t fragment[]; /* 248 bytes for all but last one */
} __packed;
struct hciCmplWriteRemoteAmpAssoc {
	uint8_t status;
	uint8_t physicalLinkHandle;
} __packed;

/* ==== BT 4.1 ==== */

#define HCI_CMD_Get_MWS_Transport_Layer_Configuration 0x000C /* complete */
struct hciCmplGetMwsTransportLayerConfigItem {
	uint8_t transportLayer;
	uint8_t numBaudRates;
} __packed;
struct hciCmplGetMwsTransportLayerConfigBandwidthItem {
	uint32_t toMwsBaudRate;
	uint32_t fromMwsBaudRate;
} __packed;
struct hciCmplGetMwsTransportLayerConfig {
	uint8_t status;
	uint8_t numTransports;
	struct hciCmplGetMwsTransportLayerConfigItem items[]; /* numTransports
								 items */
	/* this follows:
		struct hciCmplGetMwsTransportLayerConfigBandwidthItem items[] //
	   sum(items[].numbaudRates) items
	*/
} __packed;

#define HCI_CMD_Set_Triggered_Clock_Capture 0x000D /* complete */
struct hciSetTriggeredClockCapture {
	uint16_t conn;
	uint8_t enable;
	uint8_t piconetClock; /* else local clock & "conn" is ignored */
	uint8_t lpoAllowed; /* can sleep? */
	uint8_t numClockCapturesToFilter;
} __packed;
struct hciCmplSetTriggeredClockCapture {
	uint8_t status;
} __packed;

#define HCI_OGF_LE 8

/* ==== BT 4.0 ==== */

#define HCI_CMD_LE_Set_Event_Mask 0x0001 /* complete */
struct hciLeSetEventMask {
	uint64_t events; /* bitmask of HCI_LE_EVENT_* */
} __packed;
struct hciCmplLeSetEventMask {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Read_Buffer_Size 0x0002 /* complete */
struct hciCmplLeReadBufferSize {
	uint8_t status;
	uint16_t leBufferSize;
	uint8_t leNumBuffers;
} __packed;

#define HCI_CMD_LE_Read_Local_Supported_Features 0x0003 /* complete */
struct hciCmplLeReadLocalSupportedFeatures {
	uint8_t status;
	uint64_t leFeatures; /* bitmask of HCI_LE_FTR_* */
} __packed;

#define HCI_CMD_LE_Set_Random_Address 0x0005 /* complete */
struct hciLeSetRandomAddress {
	uint8_t mac[6];
} __packed;
struct hciCmplLeSetRandomAddress {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Set_Adv_Params 0x0006 /* complete */
struct hciLeSetAdvParams {
	uint16_t advIntervalMin;
	uint16_t advIntervalMax;
	uint8_t advType;
	uint8_t useRandomAddress;
	uint8_t directRandomAddress;
	uint8_t directAddr[6];
	uint8_t advChannelMap;
	uint8_t advFilterPolicy;
} __packed;
struct hciCmplLeSetAdvParams {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Read_Adv_Channel_TX_Power 0x0007 /* complete */
struct hciCmplLeReadAdvChannelTxPower {
	uint8_t status;
	uint8_t txPower; /* actually an int8_t */
} __packed;

#define HCI_CMD_LE_Set_Advertising_Data 0x0008 /* complete */
struct hciLeSetAdvData {
	uint8_t advDataLen;
	uint8_t advData[31];
} __packed;
struct hciCmplLeSetAdvData {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Set_Scan_Response_Data 0x0009 /* complete */
struct hciSetScanResponseData {
	uint8_t scanRspDataLen;
	uint8_t scanRspData[31];
} __packed;
struct hciCmplSetScanResponseData {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Set_Advertise_Enable 0x000A /* complete */
struct hciLeSetAdvEnable {
	uint8_t advOn;
} __packed;
struct hciCmplLeSetAdvEnable {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Set_Scan_Parameters 0x000B /* complete */
struct hciLeSetScanParams {
	uint8_t activeScan;
	uint16_t scanInterval; /* in units of 0.625ms, 4..0x4000 */
	uint16_t scanWindow; /* in units of 0.625ms, 4..0x4000 */
	uint8_t useOwnRandomAddr;
	uint8_t onlyAllowlist;
} __packed;
struct hciCmplLeSetScanParams {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Set_Scan_Enable 0x000C /* complete */
struct hciLeSetScanEnable {
	uint8_t scanOn;
	uint8_t filterDuplicates;
} __packed;
struct hciCmplLeSetScanEnable {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Create_Connection 0x000D /* status */
struct hciLeCreateConnection {
	uint16_t scanInterval; /* in units of 0.625ms, 4..0x4000 */
	uint16_t scanWindow; /* in units of 0.625ms, 4..0x4000 */
	uint8_t connectToAnyAllowlistedDevice; /* if so, ignore next 2 params */
	uint8_t peerRandomAddr;
	uint8_t peerMac[6];
	uint8_t useOwnRandomAddr;
	uint16_t connIntervalMin; /* in units of 1.25ms, 6..0x0C80 */
	uint16_t connIntervalMax; /* in units of 1.25ms, 6..0x0C80 */
	uint16_t connLatency; /* 0..0x1F4 */
	uint16_t supervisionTimeout; /* in units of 10ms, 0xA...0x0C80 */
	uint16_t minConnLen; /* minimum conn len needed in units of 0.625ms
				0..0xfff */
	uint16_t maxConnLen; /* minimum conn len needed in units of 0.625ms
				0..0xfff */
} __packed;

#define HCI_CMD_LE_Create_Connection_Cancel 0x000E /* complete */
struct hciCmplLeCreateConnectionCancel {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Read_Allow_List_Size 0x000F /* complete */
struct hciCmplLeReadAllowListSize {
	uint8_t status;
	uint8_t allowlistSize;
} __packed;

#define HCI_CMD_LE_Clear_Allow_List 0x0010 /* complete */
struct hciCmplLeClearAllowList {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Add_Device_To_Allow_List 0x0011 /* complete */
struct hciLeAddDeviceToAllowList {
	uint8_t randomAddr;
	uint8_t mac[6];
} __packed;
struct hciCmplLeAddDeviceToAllowList {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Remove_Device_From_Allow_List 0x0012 /* complete */
struct hciLeRemoveDeviceFromAllowList {
	uint8_t randomAddr;
	uint8_t mac[6];
} __packed;
struct hciCmplLeRemoveDeviceFromAllowList {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Connection_Update 0x0013 /* status */
struct hciLeConnectionUpdate {
	uint16_t conn;
	uint16_t connIntervalMin; /* in units of 1.25ms, 6..0x0C80 */
	uint16_t connIntervalMax; /* in units of 1.25ms, 6..0x0C80 */
	uint16_t connLatency; /* 0..0x1F4 */
	uint16_t supervisionTimeout; /* in units of 10ms, 0xA...0x0C80 */
	uint16_t minConnLen; /* minimum conn len needed in units of 0.625ms
				0..0xfff */
	uint16_t maxConnLen; /* minimum conn len needed in units of 0.625ms
				0..0xfff */
} __packed;

#define HCI_CMD_LE_Set_Host_Channel_Classification 0x0014 /* complete */
struct hciLeSetHostChannelClassification {
	uint8_t chMap[5];
} __packed;
struct hciCmplLeSetHostChannelClassification {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Read_Channel_Map 0x0015 /* complete */
struct hciLeReadChannelMap {
	uint16_t conn;
} __packed;
struct hciCmplLeReadChannelMap {
	uint8_t status;
	uint16_t conn;
	uint8_t chMap[5];
} __packed;

#define HCI_CMD_LE_Read_Remote_Used_Features 0x0016 /* status */
struct hciLeReadRemoteUsedFeatures {
	uint16_t conn;
} __packed;

#define HCI_CMD_LE_Encrypt 0x0017 /* complete */
struct hciLeEncrypt {
	uint8_t key[16];
	uint8_t plaintext[16];
} __packed;
struct hciCmplLeEncrypt {
	uint8_t status;
	uint8_t encryptedData[16];
} __packed;

#define HCI_CMD_LE_Rand 0x0018 /* complete */
struct hciCmplLeRand {
	uint8_t status;
	uint64_t rand;
} __packed;

#define HCI_CMD_LE_Start_Encryption 0x0019 /* status */
struct hciLeStartEncryption {
	uint16_t conn;
	uint64_t rand;
	uint16_t diversifier;
	uint8_t LTK[16];
} __packed;

#define HCI_CMD_LE_LTK_Request_Reply 0x001A /* complete */
struct hciLeLtkRequestReply {
	uint16_t conn;
	uint8_t LTK[16];
} __packed;
struct hciCmplLeLtkRequestReply {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_LE_LTK_Request_Negative_Reply 0x001B /* complete */
struct hciLeLtkRequestNegativeReply {
	uint16_t conn;
} __packed;
struct hciCmplLeLtkRequestNegativeReply {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_LE_Read_Supported_States 0x001C /* complete */
struct hciCmplLeReadSupportedStates {
	uint8_t status;
	uint64_t states; /* bitmask of HCI_LE_STATE_* */
} __packed;

#define HCI_CMD_LE_Receiver_Test 0x001D /* complete */
struct hciLeReceiverTest {
	uint8_t radioChannelNum; /* 2402 + radioChannelNum * 2 MHz */
} __packed;
struct hciCmplLeReceiverTest {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Transmitter_Test 0x001E /* complete */
struct hciLeTransmitterTest {
	uint8_t radioChannelNum; /* 2402 + radioChannelNum * 2 MHz */
	uint8_t lengthOfTestData;
	uint8_t testPacketDataType;
} __packed;
struct hciCmplLeTransmitterTest {
	uint8_t status;
} __packed;

#define HCI_CMD_LE_Test_End 0x001F /* complete */
struct hciCmplLeTestEnd {
	uint8_t status;
	uint16_t numPackets;
} __packed;

/* ==== BT 4.1 ==== */

#define HCI_CMD_LE_Remote_Conn_Param_Request_Reply 0x0020 /* complete */
struct hciLeRemoteConnParamRequestReply {
	uint16_t conn;
	uint16_t connIntervalMin; /* in units of 1.25ms, 6..0x0C80 */
	uint16_t connIntervalMax; /* in units of 1.25ms, 6..0x0C80 */
	uint16_t connLatency; /* 0..0x1F4 */
	uint16_t supervisionTimeout; /* in units of 10ms, 0xA...0x0C80 */
	uint16_t minConnLen; /* minimum conn len needed in units of 0.625ms
				0..0xfff */
	uint16_t maxConnLen; /* minimum conn len needed in units of 0.625ms
				0..0xfff */
} __packed;
struct hciCmplLeRemoteConnParamRequestReply {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_CMD_LE_Remote_Conn_Param_Request_Negative_Reply \
	0x0021 /* complete */
struct hciRemoteConnParamRequestNegativeReply {
	uint16_t conn;
	uint8_t reason;
} __packed;
struct hciCmplLeRemoteConnParamRequestNegativeReply {
	uint8_t status;
	uint16_t conn;
} __packed;

/* EVENTS */

/* ==== BT 1.1 ==== */

#define HCI_EVT_Inquiry_Complete 0x01
struct hciEvtInquiryComplete {
	uint8_t status;
} __packed;

#define HCI_EVT_Inquiry_Result 0x02
struct hciEvtInquiryResultItem {
	uint8_t mac[6];
	uint8_t PSRM;
	uint8_t PSPM;
	uint8_t PSM; /* obsoleted in BT 1.2+ */
	uint8_t deviceClass[3];
	uint16_t clockOffset;
} __packed;
struct hciEvtInquiryResult {
	uint8_t numResponses;
	struct hciEvtInquiryResultItem items[];
} __packed;

#define HCI_EVT_Connection_Complete 0x03
struct hciEvtConnComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t mac[6];
	uint8_t isAclLink;
	uint8_t encrypted;
} __packed;

#define HCI_EVT_Connection_Request 0x04
struct hciEvtConnRequest {
	uint8_t mac[6];
	uint8_t deviceClass[3];
	uint8_t isAclLink;
} __packed;

#define HCI_EVT_Disconnection_Complete 0x05
struct hciEvtDiscComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t reason;
} __packed;

#define HCI_EVT_Authentication_Complete 0x06
struct hciEvtAuthComplete {
	uint8_t status;
	uint16_t handle;
} __packed;

#define HCI_EVT_Remote_Name_Request_Complete 0x07
struct hciEvtRemoteNameReqComplete {
	uint8_t status;
	uint8_t mac[6];
	char name[HCI_DEV_NAME_LEN];
} __packed;

#define HCI_EVT_Encryption_Change 0x08
struct hciEvtEncrChange {
	uint8_t status;
	uint16_t conn;
	uint8_t encrOn;
} __packed;

#define HCI_EVT_Change_Connection_Link_Key_Complete 0x09
struct hciEvtChangeConnLinkKeyComplete {
	uint8_t status;
	uint16_t handle;
} __packed;

#define HCI_EVT_Master_Link_Key_Complete 0x0A
struct hciEvtMasterLinkKeyComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t usingTempKey; /* else using semi-permanent key */
} __packed;

#define HCI_EVT_Read_Remote_Supported_Features_Complete 0x0B
struct hciEvtReadRemoteSupportedFeaturesComplete {
	uint8_t status;
	uint16_t conn;
	uint64_t lmpFeatures; /* bitmask of HCI_LMP_FTR_* */
} __packed;

#define HCI_EVT_Read_Remote_Version_Complete 0x0C
struct hciEvtReadRemoteVersionComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t lmpVersion; /* HCI_VERSION_* */
	uint16_t manufName;
	uint16_t lmpSubversion;
} __packed;

#define HCI_EVT_QOS_Setup_Complete 0x0D
struct hciEvtQosSetupComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t flags;
	uint8_t serviceType;
	uint32_t tokenRate;
	uint32_t peakBandwidth;
	uint32_t latency;
	uint32_t delayVariation;
} __packed;

#define HCI_EVT_Command_Complete 0x0E
struct hciEvtCmdComplete {
	uint8_t numCmdCredits;
	uint16_t opcode;
} __packed;

#define HCI_EVT_Command_Status 0x0F
struct hciEvtCmdStatus {
	uint8_t status;
	uint8_t numCmdCredits;
	uint16_t opcode;
} __packed;

#define HCI_EVT_Hardware_Error 0x10
struct hciEvtHwError {
	uint8_t errCode;
} __packed;

#define HCI_EVT_Flush_Occurred 0x11
struct hciEvtFlushOccurred {
	uint16_t conn;
} __packed;

#define HCI_EVT_Role_Change 0x12
struct hciEvtRoleChange {
	uint8_t status;
	uint8_t mac[6];
	uint8_t amSlave;
} __packed;

#define HCI_EVT_Number_Of_Completed_Packets 0x13
struct hciEvtNumCompletedPacketsItem {
	uint16_t conn;
	uint16_t numPackets;
} __packed;
struct hciEvtNumCompletedPackets {
	uint8_t numHandles;
	struct hciEvtNumCompletedPacketsItem items[];
} __packed;

#define HCI_EVT_Mode_Change 0x14
struct hciEvtModeChange {
	uint8_t status;
	uint16_t conn;
	uint8_t mode; /* HCI_CUR_MODE_* */
	uint16_t interval; /* in units of 0.625ms 0..0xffff */
} __packed;

#define HCI_EVT_Return_Link_Keys 0x15
struct hciEvtReturnLinkKeysItem {
	uint8_t mac[6];
	uint8_t key[16];
} __packed;
struct hciEvtReturnLinkKeys {
	uint8_t numKeys;
	struct hciEvtReturnLinkKeysItem items[];
} __packed;

#define HCI_EVT_PIN_Code_Request 0x16
struct hciEvtPinCodeReq {
	uint8_t mac[6];
} __packed;

#define HCI_EVT_Link_Key_Request 0x17
struct hciEvtLinkKeyReq {
	uint8_t mac[6];
} __packed;

#define HCI_EVT_Link_Key_Notification 0x18
struct hciEvtLinkKeyNotif {
	uint8_t mac[6];
	uint8_t key[16];
	uint8_t keyType; /* HCI_KEY_TYPE_ */
} __packed;

#define HCI_EVT_Loopback_Command 0x19
/* data is the sent command, up to 252 bytes of it */

#define HCI_EVT_Data_Buffer_Overflow 0x1A
struct hciEvtDataBufferOverflow {
	uint8_t aclLink;
} __packed;

#define HCI_EVT_Max_Slots_Change 0x1B
struct hciEvtMaxSlotsChange {
	uint16_t conn;
	uint8_t lmpMaxSlots;
} __packed;

#define HCI_EVT_Read_Clock_Offset_Complete 0x1C
struct hciEvtReadClockOffsetComplete {
	uint8_t status;
	uint16_t conn;
	uint16_t clockOffset;
} __packed;

#define HCI_EVT_Connection_Packet_Type_Changed 0x1D
struct hciEvtConnPacketTypeChanged {
	uint8_t status;
	uint16_t conn;
	uint16_t packetsAllowed; /* HCI_PKT_TYP_* */
} __packed;

#define HCI_EVT_QoS_Violation 0x1E
struct hciEvtQosViolation {
	uint16_t conn;
} __packed;

#define HCI_EVT_Page_Scan_Mode_Change 0x1F /* deprecated in BT 1.2+ */
struct hciEvtPsmChange {
	uint8_t mac[6];
	uint8_t PSM;
} __packed;

#define HCI_EVT_Page_Scan_Repetition_Mode_Change 0x20
struct hciEvtPrsmChange {
	uint8_t mac[6];
	uint8_t PSRM;
} __packed;

/* ==== BT 1.2 ==== */

#define HCI_EVT_Flow_Specification_Complete 0x21
struct hciEvtFlowSpecComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t flags;
	uint8_t flowDirection;
	uint8_t serviceType;
	uint32_t tokenRate;
	uint32_t peakBandwidth;
	uint32_t latency;
} __packed;

#define HCI_EVT_Inquiry_Result_With_RSSI 0x22
struct hciEvtInquiryResultWithRssiItem {
	uint8_t mac[6];
	uint8_t PSRM;
	uint8_t PSPM;
	uint8_t deviceClass[3];
	uint16_t clockOffset;
	uint8_t RSSI; /* actually a int8_t */
} __packed;
struct hciEvtInquiryResultWithRssi {
	uint8_t numResponses;
	struct hciEvtInquiryResultWithRssiItem items[];
} __packed;

#define HCI_EVT_Read_Remote_Extended_Features_Complete 0x23
struct hciEvtReadRemoteExtFeturesComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t pageNum;
	uint8_t maxPageNum;
	uint64_t extLmpFeatures; /* HCI_LMP_EXT_FTR_P* & HCI_LMP_FTR_* */
} __packed;

#define HCI_EVT_Synchronous_Connection_Complete 0x2C
struct hciEvtSyncConnComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t mac[6];
	uint8_t linkType; /* HCI_SCO_LINK_TYPE_* */
	uint8_t interval;
	uint8_t retrWindow;
	uint16_t rxPacketLen;
	uint16_t txPacketLen;
	uint8_t airMode; /* HCI_SCO_AIR_MODE_* */
} __packed;

#define HCI_EVT_Synchronous_Connection_Changed 0x2D
struct hciEvtSyncConnChanged {
	uint8_t status;
	uint16_t conn;
	uint8_t interval;
	uint8_t retrWindow;
	uint16_t rxPacketLen;
	uint16_t txPacketLen;
} __packed;

/* ==== BT 2.1 ==== */

#define HCI_EVT_Sniff_Subrating 0x2E
struct hciEvtSniffSubrating {
	uint8_t status;
	uint16_t conn;
	uint16_t maxTxLatency;
	uint16_t maxRxLatency;
	uint16_t minRemoteTimeout;
	uint16_t minLocalTimeout;
} __packed;

#define HCI_EVT_Extended_Inquiry_Result 0x2F
struct hciEvtExtendedInquiryResult {
	uint8_t numResponses; /* must be 1 */
	uint8_t mac[6];
	uint8_t PSRM;
	uint8_t reserved;
	uint8_t deviceClass[3];
	uint16_t clockOffset;
	uint8_t RSSI; /* actually a int8_t */
	uint8_t EIR[240];
} __packed;

#define HCI_EVT_Encryption_Key_Refresh_Complete 0x30
struct hciEvtEncrKeyRefreshComplete {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_EVT_IO_Capability_Request 0x31
struct hciEvtIoCapRequest {
	uint8_t mac[6];
} __packed;

#define HCI_EVT_IO_Capability_Response 0x32
struct hciEvtIoCapResponse {
	uint8_t mac[6];
	uint8_t ioCapability; /* HCI_DISPLAY_CAP_* */
	uint8_t oobDataPresent;
	uint8_t authReqments; /* HCI_AUTH_REQMENT_ */
} __packed;

#define HCI_EVT_User_Confirmation_Request 0x33
struct hciEvtUserConfRequest {
	uint8_t mac[6];
	uint32_t numericValue;
} __packed;

#define HCI_EVT_User_Passkey_Request 0x34
struct hciEvtUserPasskeyRequest {
	uint8_t mac[6];
} __packed;

#define HCI_EVT_Remote_OOB_Data_Request 0x35
struct hciEvtRemoteOobRequest {
	uint8_t mac[6];
} __packed;

#define HCI_EVT_Simple_Pairing_Complete 0x36
struct hciEvtSimplePairingComplete {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_EVT_Link_Supervision_Timeout_Changed 0x38
struct hciEvtLinkSupervisionTimeoutChanged {
	uint16_t conn;
	uint16_t timeout; /* in units of 0.625 ms 1..0xffff */
} __packed;

#define HCI_EVT_Enhanced_Flush_Complete 0x39
struct hciEvtEnahncedFlushComplete {
	uint16_t conn;
} __packed;

#define HCI_EVT_User_Passkey_Notification 0x3B
struct hciEvtUserPasskeyNotif {
	uint8_t mac[6];
	uint32_t passkey;
} __packed;

#define HCI_EVT_Keypress_Notification 0x3C
struct hciEvtKeypressNotification {
	uint8_t mac[6];
	uint8_t notifType; /* HCI_SSP_KEY_ENTRY_* */
} __packed;

#define HCI_EVT_Remote_Host_Supported_Features_Notification 0x3D
struct hciEvtRemoteHostSupportedFeatures {
	uint8_t mac[6];
	uint64_t hostSupportedFeatures; /* HCI_LMP_FTR_* */
} __packed;

/* ==== BT 3.0 ==== */

#define HCI_EVT_Physical_Link_Complete 0x40
struct hciEvtPhysLinkComplete {
	uint8_t status;
	uint8_t physLinkHandle;
} __packed;

#define HIC_EVT_Channel_Selected 0x41
struct hciEvtChannelSelected {
	uint8_t physLinkHandle;
} __packed;

#define HCI_EVT_Disconnection_Physical_Link_Complete 0x42
struct hciEvtDiscPhysLinkComplete {
	uint8_t status;
	uint8_t physLinkHandle;
	uint8_t reason;
} __packed;

#define HCI_EVT_Physical_Link_Loss_Early_Warning 0x43
struct hciEvtDiscPhysLinkLossEralyWarning {
	uint8_t physLinkHandle;
	uint8_t lossReason;
} __packed;

#define HCI_EVT_Physical_Link_Recovery 0x44
struct hciEvtDiscPhysLinkRecovery {
	uint8_t physLinkHandle;
} __packed;

#define HCI_EVT_Logical_Link_Complete 0x45
struct hciEvtLogicalLinkComplete {
	uint8_t status;
	uint16_t logicalLinkHandle;
	uint8_t physLinkHandle;
	uint8_t txFlowSpecID;
} __packed;

#define HCI_EVT_Disconnection_Logical_Link_Complete 0x46
struct hciEvtDiscLogicalLinkComplete {
	uint8_t status;
	uint16_t logicalLinkHandle;
	uint8_t reason;
} __packed;

#define HCI_EVT_Flow_Spec_Modify_Complete 0x47
struct hciEvtFlowSpecModifyComplete {
	uint8_t status;
	uint16_t conn;
} __packed;

#define HCI_EVT_Number_Of_Completed_Data_Blocks 0x48
struct hciEvtNumCompletedDataBlocksItem {
	uint16_t conn;
	uint16_t numPackets;
} __packed;
struct hciEvtNumCompletedDataBlocks {
	uint16_t totalNumBlocks;
	uint8_t numberOfHandles;
	struct hciEvtNumCompletedDataBlocksItem items[];
} __packed;

#define HCI_EVT_AMP_Start_Test 0x49
struct hciEvtAmpStartTest {
	uint8_t status;
	uint8_t scenario;
} __packed;

#define HCI_EVT_AMP_Test_End 0x4A
struct hciEvtAmpTestEnd {
	uint8_t status;
	uint8_t scenario;
} __packed;

#define HCI_EVT_AMP_Receiver_Report 0x4B
struct hciEvtampReceiverReport {
	uint8_t controllerType;
	uint8_t reason;
	uint32_t eventType;
	uint16_t numberOfFrames;
	uint16_t numberOfErrorFrames;
	uint32_t numberOfBits;
	uint32_t numberOfErrorBits;
} __packed;

#define HCI_EVT_Short_Range_Mode_Change_Complete 0x4C
struct hciEvtshortRangeModeChangeComplete {
	uint8_t status;
	uint8_t physLinkHandle;
	uint8_t shortRangeModeOn;
} __packed;

#define HCI_EVT_AMP_Status_Change 0x4D
struct hciEvtAmpStatusChange {
	uint8_t status;
	uint8_t ampStatus;
} __packed;

/* ==== BT 4.0 ==== */

#define HCI_EVT_LE_Meta 0x3E
struct hciEvtLeMeta {
	uint8_t subevent;
} __packed;

#define HCI_EVTLE_Connection_Complete 0x01
struct hciEvtLeConnectionComplete {
	uint8_t status;
	uint16_t conn;
	uint8_t amSlave;
	uint8_t peerAddrRandom;
	uint8_t peerMac[6];
	uint16_t connInterval; /* in units of 1.25 ms 6..0x0C80 */
	uint16_t connLatency; /* 0..0x01f3 */
	uint16_t supervisionTimeout; /* inunit sof 10ms, 0xA..0x0C80 */
	uint8_t masterClockAccuracy; /* HCI_MCA_* */
} __packed;

#define HCI_EVTLE_Advertising_Report 0x02
struct hciEvtLeAdvReportItem {
	uint8_t advType; /* HCI_ADV_TYPE_* */
	uint8_t randomAddr;
	uint8_t mac[6];
	uint8_t dataLen;
	uint8_t data[];
	/*  int8_t RSSI <-- this cannot be here due to variable data len, but in
	 * reality it is there */
} __packed;
struct hciEvtLeAdvReport {
	uint8_t numReports;
	/* struct hciEvtLeAdvReportItem items[]; <- this cannot be here since
	 * data length is variable */
} __packed;

#define HCI_EVTLE_Connection_Update_Complete 0x03
struct hciEvtLeConnectionUpdateComplete {
	uint8_t status;
	uint16_t conn;
	uint16_t connInterval; /* in units of 1.25 ms 6..0x0C80 */
	uint16_t connLatency; /* 0..0x01f3 */
	uint16_t supervisionTimeout; /* inunit sof 10ms, 0xA..0x0C80 */
} __packed;

#define HCI_EVTLE_Read_Remote_Used_Features_Complete 0x04
struct hciEvtLeReadRemoteFeaturesComplete {
	uint8_t status;
	uint16_t conn;
	uint64_t leFeatures; /* bitmask of HCI_LE_FTR_* */
} __packed;

#define HCI_EVTLE_LTK_Request 0x05
struct hciEvtLeLtkRequest {
	uint16_t conn;
	uint64_t randomNum;
	uint16_t diversifier;
} __packed;

/* ==== BT 4.1 ==== */

#define HCI_EVTLE_Read_Remote_Connection_Parameter_Request 0x06
struct hciEvtLeReadRemoteConnParamRequest {
	uint16_t conn;
	uint16_t connIntervalMin; /* in units of 1.25 ms 6..0x0C80 */
	uint16_t connIntervalMax; /* in units of 1.25 ms 6..0x0C80 */
	uint16_t connLatency; /* 0..0x01f3 */
	uint16_t supervisionTimeout; /* inunit sof 10ms, 0xA..0x0C80 */
} __packed;

#define HCI_EVT_Triggered_Clock_Capture 0x4E
struct hciEvtTriggeredClockCapture {
	uint16_t conn;
	uint8_t piconetClock;
	uint32_t clock;
	uint16_t slotOffset;
} __packed;

#define HCI_EVT_Synchronization_Train_Complete 0x4F
struct hciEvtSyncTrainComplete {
	uint8_t status;
} __packed;

#define HCI_EVT_Synchronization_Train_Received 0x50
struct hciEvtSyncTrainReceived {
	uint8_t status;
	uint8_t mac[6];
	uint32_t offset;
	uint8_t afhChannelMap[10];
	uint8_t ltAddr;
	uint32_t nextBroadcastInstant;
	uint16_t connectionlessSlaveBroadcastInterval;
	uint8_t serviceData;
} __packed;

#define HCI_EVT_Connectionless_Slave_Broadcast_Receive 0x51
struct hciEvtConnectionlessSlaveBroadcastReceive {
	uint8_t mac[6];
	uint8_t ltAddr;
	uint32_t clk;
	uint32_t offset;
	uint8_t rxFailed;
	uint8_t fragment; /* HCI_CONNLESS_FRAG_TYPE_* */
	uint8_t dataLen;
	/* data */
} __packed;

#define HCI_EVT_Connectionless_Slave_Broadcast_Timeout 0x52
struct hciEvtConnectionlessSlaveBroadcastTimeout {
	uint8_t mac[6];
	uint8_t ltAddr;
} __packed;

#define HCI_EVT_Truncated_Page_Complete 0x53
struct hciEvtTruncatedPageComplete {
	uint8_t status;
	uint8_t mac[6];
} __packed;

#define HCI_EVT_Slave_Page_Response_Timeout 0x54

#define HCI_EVT_Connless_Slave_Broadcast_Channel_Map_Change 0x55
struct hciEvtConnlessSlaveBroadcastChannelMapChange {
	uint8_t map[10];
} __packed;

#define HCI_EVT_Inquiry_Response_Notification 0x56
struct hciEvtInquiryResponseNotif {
	uint8_t lap[3];
	uint8_t RSSI; /* actually an int8_t */
} __packed;

#define HCI_EVT_Authenticated_Payload_Timeout_Expired 0x57
struct hciEvtAuthedPayloadTimeoutExpired {
	uint16_t conn;
} __packed;

/* ERROR CODES */

/* ==== BT 1.1 ==== */

#define HCI_SUCCESS 0x00
#define HCI_ERR_Unknown_HCI_Command 0x01
#define HCI_ERR_No_Connection 0x02
#define HCI_ERR_Hardware_Failure 0x03
#define HCI_ERR_Page_Timeout 0x04
#define HCI_ERR_Authentication_Failure 0x05
#define HCI_ERR_Key_Missing 0x06
#define HCI_ERR_Memory_Full 0x07
#define HCI_ERR_Connection_Timeout 0x08
#define HCI_ERR_Max_Number_Of_Connections 0x09
#define HCI_ERR_Max_Number_Of_SCO_Connections_To_A_Device 0x0A
#define HCI_ERR_ACL_Connection_Already_Exists 0x0B
#define HCI_ERR_Command_Disallowed 0x0C
#define HCI_ERR_Host_Rejected_Due_To_Limited_Resources 0x0D
#define HCI_ERR_Host_Rejected_Due_To_Security_Reasons 0x0E
#define HCI_ERR_Host_Rejected_Remote_Device_Personal_Device 0x0F
#define HCI_ERR_Host_Timeout 0x10
#define HCI_ERR_Unsupported_Feature_Or_Parameter_Value 0x11
#define HCI_ERR_Invalid_HCI_Command_Parameters 0x12
#define HCI_ERR_Other_End_Terminated_Connection_User_Requested 0x13
#define HCI_ERR_Other_End_Terminated_Connection_Low_Resources 0x14
#define HCI_ERR_Other_End_Terminated_Connection_Soon_Power_Off 0x15
#define HCI_ERR_Connection_Terminated_By_Local_Host 0x16
#define HCI_ERR_Repeated_Attempts 0x17
#define HCI_ERR_Pairing_Not_Allowed 0x18
#define HCI_ERR_Unknown_LMP_PDU 0x19
#define HCI_ERR_Unsupported_Remote_Feature 0x1A
#define HCI_ERR_SCO_Offset_Rejected 0x1B
#define HCI_ERR_SCO_Interval_Rejected 0x1C
#define HCI_ERR_SCO_Air_Mode_Rejected 0x1D
#define HCI_ERR_Invalid_LMP_Parameters 0x1E
#define HCI_ERR_Unspecified_Error 0x1F
#define HCI_ERR_Unsupported_LMP_Parameter 0x20
#define HCI_ERR_Role_Change_Not_Allowed 0x21
#define HCI_ERR_LMP_Response_Timeout 0x22
#define HCI_ERR_LMP_Error_Transaction_Collision 0x23
#define HCI_ERR_LMP_PDU_Not_Allowed 0x24
#define HCI_ERR_Encryption_Mode_Not_Acceptable 0x25
#define HCI_ERR_Unit_Key_Used 0x26
#define HCI_ERR_QoS_Not_Supported 0x27
#define HCI_ERR_Instant_Passed 0x28
#define HCI_ERR_Pairing_With_Unit_Key_Not_Supported 0x29

/* ==== BT 1.2 ==== */

#define HCI_ERR_Different_Transaction_Collision 0x2A
#define HCI_ERR_QoS_Unacceptable_Parameter 0x2C
#define HCI_ERR_QoS_Rejected 0x2D
#define HCI_ERR_Channel_Classification_Not_Supported 0x2E
#define HCI_ERR_Insufficient_Security 0x2F
#define HCI_ERR_Parameter_Out_Of_Mandatory_Range 0x30
#define HCI_ERR_Role_Switch_Pending 0x33
#define HCI_ERR_Reserved_Slot_Violation 0x34
#define HIC_ERR_Role_Switch_Failed 0x35

/* ==== BT 2.1 ==== */

#define HCI_ERR_EIR_Too_Large 0x36
#define HCI_ERR_SSP_Not_Supported_By_Host 0x37
#define HCI_ERR_Host_Busy_Pairing 0x38

/* ==== BT 3.0 ==== */

#define HCI_ERR_Connection_Rejected_No_Suitable_Channel_Found 0x39
#define HCI_ERR_Controller_Busy 0x3A

/* ==== BT 4.0 ==== */

#define HCI_ERR_Unacceptable_Connection_Interval 0x3B
#define HCI_ERR_Directed_Advertising_Timeout 0x3C
#define HCI_ERR_Connection_Terminated_Due_To_MIC_Failure 0x3D
#define HCI_ERR_Connection_Failed_To_To_Established 0x3E
#define HCI_ERR_MAC_Connection_Failed 0x3F

/* ==== BT 4.1 ==== */

#define HCI_ERR_CoarseClock_AdjFailed_Will_Try_clock_Dragging 0x40

#ifdef __cplusplus
}
#endif

#endif
