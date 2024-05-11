/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BLUETOOTH_LE_LL_H
#define __CROS_EC_BLUETOOTH_LE_LL_H

#include "btle_hci_int.h"
#include "common.h"

enum ll_state_t {
	UNINITIALIZED,
	STANDBY,
	SCANNING,
	ADVERTISING,
	INITIATING,
	CONNECTION,
	TEST_RX,
	TEST_TX,
};

#define LL_ADV_INTERVAL_UNIT_US 625
#define LL_ADV_TIMEOUT_UNIT_US 1000000

#define LL_ADV_DIRECT_INTERVAL_US 3750 /* 3.75 ms */
#define LL_ADV_DIRECT_TIMEOUT_US 1280000 /* 1.28 s */

#define LL_MAX_DATA_PACKET_LENGTH 27
#define LL_MAX_DATA_PACKETS 4

/* BTLE Spec 4.0: Vol 6, Part B, Section 4.5.3 */
#define TRANSMIT_WINDOW_OFFSET_CONSTANT 1250

#define LL_MAX_BUFFER_SIZE (LL_MAX_DATA_PACKET_LENGTH * LL_MAX_DATA_PACKETS)

#define LL_SUPPORTED_FEATURES                                               \
	(HCI_LE_FTR_ENCRYPTION | HCI_LE_FTR_CONNECTION_PARAMETERS_REQUEST | \
	 HCI_LE_FTR_EXTENDED_REJECT_INDICATION |                            \
	 HCI_LE_FTR_SLAVE_INITIATED_FEATURES_EXCHANGE)

#define LL_SUPPORTED_STATES                                       \
	(HCI_LE_STATE_NONCON_ADV | HCI_LE_STATE_SCANNABLE_ADV |   \
	 HCI_LE_STATE_CONNECTABLE_ADV | HCI_LE_STATE_DIRECT_ADV | \
	 HCI_LE_STATE_PASSIVE_SCAN | HCI_LE_STATE_ACTIVE_SCAN |   \
	 HCI_LE_STATE_INITIATE | HCI_LE_STATE_SLAVE)

/*
 * 4.6.1 LE Encryption
 * A controller that supports LE Encryption shall support the following sections
 * within this document:
 * - LL_ENC_REQ (Section 2.4.2.4)
 * - LL_ENC_RSP (Section 2.4.2.5)
 * - LL_START_ENC_REQ (Section 2.4.2.6)
 * - LL_START_ENC_RSP (Section 2.4.2.7)
 * - LL_PAUSE_ENC_REQ (Section 2.4.2.11)
 * - LL_PAUSE_ENC_RSP (Section 2.4.2.12)
 * - Encryption Start Procedure (Section 5.1.3.1)
 * - Encryption Pause Procedure (Section 5.1.3.2)
 */

/*Link Layer Control PDU Opcodes */
#define LL_CONNECTION_UPDATE_REQ 0x00
#define LL_CHANNEL_MAP_REQ 0x01
#define LL_TERMINATE_IND 0x02
#define LL_ENC_REQ 0x03
#define LL_ENC_RSP 0x04
#define LL_START_ENC_REQ 0x05
#define LL_START_ENC_RSP 0x06
#define LL_UNKNOWN_RSP 0x07
#define LL_FEATURE_REQ 0x08
#define LL_FEATURE_RSP 0x09
#define LL_PAUSE_ENC_REQ 0x0A
#define LL_PAUSE_ENC_RSP 0x0B
#define LL_VERSION_IND 0x0C
#define LL_REJECT_IND 0x0D
#define LL_SLAVE_FEATURE_REQ 0x0E
#define LL_CONNECTION_PARAM_REQ 0x0F
#define LL_CONNECTION_PARAM_RSP 0x10
#define LL_REJECT_IND_EXT 0x11
#define LL_PING_REQ 0x12
#define LL_PING_RSP 0x13

/* BLE 4.1 Vol 6 2.3.3.1 Connection information */
#define CONNECT_REQ_INITA_LEN 6
#define CONNECT_REQ_ADVA_LEN 6
#define CONNECT_REQ_ACCESS_ADDR_LEN 4
#define CONNECT_REQ_CRC_INIT_VAL_LEN 3
#define CONNECT_REQ_WIN_SIZE_LEN 1
#define CONNECT_REQ_WIN_OFFSET_LEN 2
#define CONNECT_REQ_INTERVAL_LEN 2
#define CONNECT_REQ_LATENCY_LEN 2
#define CONNECT_REQ_TIMEOUT_LEN 2
#define CONNECT_REQ_CHANNEL_MAP_LEN 5
#define CONNECT_REQ_HOP_INCREMENT_AND_SCA_LEN 1
struct ble_connection_params {
	uint8_t init_a[CONNECT_REQ_INITA_LEN];
	uint8_t adv_a[CONNECT_REQ_ADVA_LEN];
	uint32_t access_addr;
	uint32_t crc_init_val;
	uint8_t win_size;
	uint16_t win_offset;
	uint16_t interval;
	uint16_t latency;
	uint16_t timeout;
	uint64_t channel_map;
	uint8_t hop_increment;
	uint8_t sleep_clock_accuracy;
	uint32_t transmitWindowOffset;
	uint32_t transmitWindowSize;
	uint32_t connInterval;
	uint16_t connLatency;
	uint32_t connSupervisionTimeout;
};

uint8_t ll_reset(void);

uint8_t ll_set_tx_power(uint8_t *params);

/* LE Information */
uint8_t ll_read_buffer_size(uint8_t *return_params);
uint8_t ll_read_local_supported_features(uint8_t *return_params);
uint8_t ll_read_supported_states(uint8_t *return_params);
uint8_t ll_set_host_channel_classification(uint8_t *params);

/* Advertising */
uint8_t ll_set_advertising_params(uint8_t *params);
uint8_t ll_read_tx_power(void);
uint8_t ll_set_adv_data(uint8_t *params);
uint8_t ll_set_scan_response_data(uint8_t *params);
uint8_t ll_set_advertising_enable(uint8_t *params);

uint8_t ll_set_random_address(uint8_t *params);

/* Scanning */
uint8_t ll_set_scan_enable(uint8_t *params);
uint8_t ll_set_scan_params(uint8_t *params);

/* Allow List */
uint8_t ll_clear_allow_list(void);
uint8_t ll_read_allow_list_size(uint8_t *return_params);
uint8_t ll_add_device_to_allow_list(uint8_t *params);
uint8_t ll_remove_device_from_allow_list(uint8_t *params);

/* Connections */
uint8_t ll_read_remote_used_features(uint8_t *params);

/* RF Phy Testing */
uint8_t ll_receiver_test(uint8_t *params);
uint8_t ll_transmitter_test(uint8_t *params);
uint8_t ll_test_end(uint8_t *return_params);

void ll_ble_test_rx(void);
void ll_ble_test_rx(void);

#endif /* __CROS_EC_BLUETOOTH_LE_LL_H */