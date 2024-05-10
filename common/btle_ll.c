/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bluetooth_le.h"
#include "bluetooth_le_ll.h"
#include "btle_hci_int.h"
#include "console.h"
#include "radio.h"
#include "radio_test.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_BLUETOOTH_LL_DEBUG

#define CPUTS(outstr) cputs(CC_BLUETOOTH_LL, outstr)
#define CPRINTS(format, args...) cprints(CC_BLUETOOTH_LL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_BLUETOOTH_LL, format, ##args)

#else /* CONFIG_BLUETOOTH_LL_DEBUG */

#define CPUTS(outstr)
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)

#endif /* CONFIG_BLUETOOTH_LL_DEBUG */

/* Link Layer */

enum ll_state_t ll_state = UNINITIALIZED;

static struct hciLeSetAdvParams ll_adv_params;
static struct hciLeSetScanParams ll_scan_params;
static int ll_adv_interval_us;
static int ll_adv_timeout_us;

static struct ble_pdu ll_adv_pdu;
static struct ble_pdu ll_scan_rsp_pdu;
static struct ble_pdu tx_packet_1;
static struct ble_pdu *packet_tb_sent;
static struct ble_connection_params conn_params;
static int connection_initialized;
static struct remapping_table remap_table;

static uint64_t receive_time, last_receive_time;
static uint8_t num_consecutive_failures;

static uint32_t tx_end, tx_rsp_end, time_of_connect_req;
struct ble_pdu ll_rcv_packet;
static uint32_t ll_conn_events;
static uint32_t errors_recovered;

int ll_power;
uint8_t is_first_data_packet;

static uint64_t ll_random_address = 0xC5BADBADBAD1; /* Uninitialized */
static uint64_t ll_public_address = 0xC5BADBADBADF; /* Uninitialized */
static uint8_t ll_channel_map[5] = { 0xff, 0xff, 0xff, 0xff, 0x1f };

static uint8_t ll_filter_duplicates;

int ll_pseudo_rand(int max_plus_one)
{
	static uint32_t lfsr = 0x55555;
	int lsb = lfsr & 1;

	lfsr = lfsr >> 1;
	if (lsb)
		lfsr ^= 0x80020003; /* Bits 32, 22, 2, 1 */
	return lfsr % max_plus_one;
}

uint8_t ll_set_tx_power(uint8_t *params)
{
	/* Add checking */
	ll_power = params[0];
	return HCI_SUCCESS;
}

uint8_t ll_read_tx_power(void)
{
	return ll_power;
}

/* LE Information */
uint8_t ll_read_buffer_size(uint8_t *return_params)
{
	return_params[0] = LL_MAX_DATA_PACKET_LENGTH & 0xff;
	return_params[1] = (LL_MAX_DATA_PACKET_LENGTH >> 8) & 0xff;
	return_params[2] = LL_MAX_DATA_PACKETS;
	return HCI_SUCCESS;
}

uint8_t ll_read_local_supported_features(uint8_t *return_params)
{
	uint64_t supported_features = LL_SUPPORTED_FEATURES;

	memcpy(return_params, &supported_features, sizeof(supported_features));
	return HCI_SUCCESS;
}

uint8_t ll_read_supported_states(uint8_t *return_params)
{
	uint64_t supported_states = LL_SUPPORTED_STATES;

	memcpy(return_params, &supported_states, sizeof(supported_states));
	return HCI_SUCCESS;
}

uint8_t ll_set_host_channel_classification(uint8_t *params)
{
	memcpy(ll_channel_map, params, sizeof(ll_channel_map));
	return HCI_SUCCESS;
}

/* Advertising */
uint8_t ll_set_scan_response_data(uint8_t *params)
{
	if (params[0] > BLE_MAX_ADV_PAYLOAD_OCTETS)
		return HCI_ERR_Invalid_HCI_Command_Parameters;

	if (ll_state == ADVERTISING)
		return HCI_ERR_Controller_Busy;

	memcpy(&ll_scan_rsp_pdu.payload[BLUETOOTH_ADDR_OCTETS], &params[1],
	       params[0]);
	ll_scan_rsp_pdu.header.adv.length = params[0] + BLUETOOTH_ADDR_OCTETS;

	return HCI_SUCCESS;
}

uint8_t ll_set_adv_data(uint8_t *params)
{
	if (params[0] > BLE_MAX_ADV_PAYLOAD_OCTETS)
		return HCI_ERR_Invalid_HCI_Command_Parameters;

	if (ll_state == ADVERTISING)
		return HCI_ERR_Controller_Busy;

	/* Skip the address */
	memcpy(&ll_adv_pdu.payload[BLUETOOTH_ADDR_OCTETS], &params[1],
	       params[0]);
	ll_adv_pdu.header.adv.length = params[0] + BLUETOOTH_ADDR_OCTETS;

	return HCI_SUCCESS;
}

uint8_t ll_reset(void)
{
	ll_state = UNINITIALIZED;
	radio_disable();

	ble_radio_clear_allow_list();

	return HCI_SUCCESS;
}

static uint8_t ll_state_change_request(enum ll_state_t next_state)
{
	/* Initialize the radio if it hasn't been initialized */
	if (ll_state == UNINITIALIZED) {
		if (ble_radio_init(BLE_ADV_ACCESS_ADDRESS, BLE_ADV_CRCINIT) !=
		    EC_SUCCESS)
			return HCI_ERR_Hardware_Failure;
		ll_state = STANDBY;
	}

	/* Only change states when the link layer is in STANDBY */
	if (next_state != STANDBY && ll_state != STANDBY)
		return HCI_ERR_Controller_Busy;

	ll_state = next_state;

	return HCI_SUCCESS;
}

uint8_t ll_set_advertising_enable(uint8_t *params)
{
	uint8_t rv;

	if (params[0]) {
		rv = ll_state_change_request(ADVERTISING);
		if (rv == HCI_SUCCESS)
			task_wake(TASK_ID_BLE_LL);
	} else {
		rv = ll_state_change_request(STANDBY);
	}

	return rv;
}

uint8_t ll_set_scan_enable(uint8_t *params)
{
	uint8_t rv;

	if (params[0]) {
		ll_filter_duplicates = params[1];
		rv = ll_state_change_request(SCANNING);
		if (rv == HCI_SUCCESS)
			task_wake(TASK_ID_BLE_LL);
	} else {
		rv = ll_state_change_request(STANDBY);
	}

	return HCI_SUCCESS;
}

void set_empty_data_packet(struct ble_pdu *pdu)
{
	/* LLID == 1 means incomplete or empty data packet */
	pdu->header.data.llid = 1;
	pdu->header.data.nesn = 1;
	pdu->header.data.sn = 0;
	pdu->header.data.md = 0;
	pdu->header.data.length = 0;
	pdu->header_type_adv = 0;
}

/* Connection state */

/**
 * This function serves to take data from a CONNECT_REQ packet and copy it
 * into a struct, conn_params, which defines the parameter of the connection.
 * It also fills a remapping table, another essential element of the link
 * layer connection.
 */
uint8_t initialize_connection(void)
{
	int cur_offset = 0, i = 0;
	uint8_t final_octet = 0;
	uint8_t remap_arr[5];
	uint8_t *payload_start = (uint8_t *)(ll_rcv_packet.payload);

	num_consecutive_failures = 0;

	/* Copy data into the appropriate portions of memory */
	memcpy((uint8_t *)&(conn_params.init_a), payload_start,
	       CONNECT_REQ_INITA_LEN);
	cur_offset += CONNECT_REQ_INITA_LEN;

	memcpy((uint8_t *)&(conn_params.adv_a), payload_start + cur_offset,
	       CONNECT_REQ_ADVA_LEN);
	cur_offset += CONNECT_REQ_ADVA_LEN;

	memcpy(&(conn_params.access_addr), payload_start + cur_offset,
	       CONNECT_REQ_ACCESS_ADDR_LEN);
	cur_offset += CONNECT_REQ_ACCESS_ADDR_LEN;

	conn_params.crc_init_val = 0;
	memcpy(&(conn_params.crc_init_val), payload_start + cur_offset,
	       CONNECT_REQ_CRC_INIT_VAL_LEN);
	cur_offset += CONNECT_REQ_CRC_INIT_VAL_LEN;

	memcpy(&(conn_params.win_size), payload_start + cur_offset,
	       CONNECT_REQ_WIN_SIZE_LEN);
	cur_offset += CONNECT_REQ_WIN_SIZE_LEN;

	memcpy(&(conn_params.win_offset), payload_start + cur_offset,
	       CONNECT_REQ_WIN_OFFSET_LEN);
	cur_offset += CONNECT_REQ_WIN_OFFSET_LEN;

	memcpy(&(conn_params.interval), payload_start + cur_offset,
	       CONNECT_REQ_INTERVAL_LEN);
	cur_offset += CONNECT_REQ_INTERVAL_LEN;

	memcpy(&(conn_params.latency), payload_start + cur_offset,
	       CONNECT_REQ_LATENCY_LEN);
	cur_offset += CONNECT_REQ_LATENCY_LEN;

	memcpy(&(conn_params.timeout), payload_start + cur_offset,
	       CONNECT_REQ_TIMEOUT_LEN);
	cur_offset += CONNECT_REQ_TIMEOUT_LEN;

	conn_params.channel_map = 0;
	memcpy(&(conn_params.channel_map), payload_start + cur_offset,
	       CONNECT_REQ_CHANNEL_MAP_LEN);
	cur_offset += CONNECT_REQ_CHANNEL_MAP_LEN;

	memcpy(&final_octet, payload_start + cur_offset,
	       CONNECT_REQ_HOP_INCREMENT_AND_SCA_LEN);

	/* last  5 bits of final_octet: */
	conn_params.hop_increment = final_octet & 0x1f;
	/* first 3 bits of final_octet: */
	conn_params.sleep_clock_accuracy = (final_octet & 0xe0) >> 5;

	/* Set up channel mapping table */
	for (i = 0; i < 5; ++i)
		remap_arr[i] = *(((uint8_t *)&(conn_params.channel_map)) + i);
	fill_remapping_table(&remap_table, remap_arr,
			     conn_params.hop_increment);

	/* Calculate transmission window parameters */
	conn_params.transmitWindowSize = conn_params.win_size * 1250;
	conn_params.transmitWindowOffset = conn_params.win_offset * 1250;
	conn_params.connInterval = conn_params.interval * 1250;
	/* The following two lines convert ms -> microseconds */
	conn_params.connLatency = 1000 * conn_params.latency;
	conn_params.connSupervisionTimeout = 10000 * conn_params.timeout;
	/* All these times are in microseconds! */

	/* Check for common transmission errors */
	if (conn_params.hop_increment < 5 || conn_params.hop_increment > 16) {
		for (i = 0; i < 5; ++i)
			CPRINTF("ERROR!! ILLEGAL HOP_INCREMENT!!\n");
		return HCI_ERR_Invalid_LMP_Parameters;
	}

	is_first_data_packet = 1;
	return HCI_SUCCESS;
}

/* Allow List */
uint8_t ll_clear_allow_list(void)
{
	if (ble_radio_clear_allow_list() == EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Hardware_Failure;
}

uint8_t ll_read_allow_list_size(uint8_t *return_params)
{
	if (ble_radio_read_allow_list_size(return_params) == EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Hardware_Failure;
}

uint8_t ll_add_device_to_allow_list(uint8_t *params)
{
	if (ble_radio_add_device_to_allow_list(&params[1], params[0]) ==
	    EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Host_Rejected_Due_To_Limited_Resources;
}

uint8_t ll_remove_device_from_allow_list(uint8_t *params)
{
	if (ble_radio_remove_device_from_allow_list(&params[1], params[0]) ==
	    EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Hardware_Failure;
}

/* Connections */
uint8_t ll_read_remote_used_features(uint8_t *params)
{
	uint16_t handle = params[0] | (((uint16_t)params[1]) << 8);

	CPRINTS("Read remote used features for handle %d", handle);
	/* Check handle */
	return HCI_SUCCESS;
}

/* RF PHY Testing */
static int ll_test_packets;

uint8_t ll_receiver_test(uint8_t *params)
{
	int rv;

	ll_test_packets = 0;

	/* See if the link layer is busy */
	rv = ll_state_change_request(TEST_RX);
	if (rv)
		return rv;

	rv = ble_test_rx_init(params[0]);
	if (rv)
		return rv;

	CPRINTS("Start Rx test");
	task_wake(TASK_ID_BLE_LL);

	return HCI_SUCCESS;
}

uint8_t ll_transmitter_test(uint8_t *params)
{
	int rv;

	ll_test_packets = 0;

	/* See if the link layer is busy */
	rv = ll_state_change_request(TEST_TX);
	if (rv)
		return rv;

	rv = ble_test_tx_init(params[0], params[1], params[2]);
	if (rv)
		return rv;

	CPRINTS("Start Tx test");
	task_wake(TASK_ID_BLE_LL);

	return HCI_SUCCESS;
}

uint8_t ll_test_end(uint8_t *return_params)
{
	CPRINTS("End (%d packets)", ll_test_packets);

	ble_test_stop();

	if (ll_state == TEST_RX) {
		return_params[0] = ll_test_packets & 0xff;
		return_params[1] = (ll_test_packets >> 8);
		ll_test_packets = 0;
	} else {
		return_params[0] = 0;
		return_params[1] = 0;
		ll_test_packets = 0;
	}
	return ll_reset();
}

uint8_t ll_set_random_address(uint8_t *params)
{
	/* No checking.  The host should know the rules. */
	memcpy(&ll_random_address, params,
	       sizeof(struct hciLeSetRandomAddress));
	return HCI_SUCCESS;
}

uint8_t ll_set_scan_params(uint8_t *params)
{
	if (ll_state == SCANNING)
		return HCI_ERR_Controller_Busy;

	memcpy(&ll_scan_params, params, sizeof(struct hciLeSetScanParams));

	return HCI_SUCCESS;
}

uint8_t ll_set_advertising_params(uint8_t *params)
{
	if (ll_state == ADVERTISING)
		return HCI_ERR_Controller_Busy;

	memcpy(&ll_adv_params, params, sizeof(struct hciLeSetAdvParams));

	switch (ll_adv_params.advType) {
	case BLE_ADV_HEADER_PDU_TYPE_ADV_NONCONN_IND:
	case BLE_ADV_HEADER_PDU_TYPE_ADV_SCAN_IND:
		if (ll_adv_params.advIntervalMin <
		    (100000 / LL_ADV_INTERVAL_UNIT_US)) /* 100ms */
			return HCI_ERR_Invalid_HCI_Command_Parameters;
	/* Fall through */
	case BLE_ADV_HEADER_PDU_TYPE_ADV_IND:
		if (ll_adv_params.advIntervalMin > ll_adv_params.advIntervalMax)
			return HCI_ERR_Invalid_HCI_Command_Parameters;
		if (ll_adv_params.advIntervalMin <
			    (20000 / LL_ADV_INTERVAL_UNIT_US) || /* 20ms */
		    ll_adv_params.advIntervalMax >
			    (10240000 / LL_ADV_INTERVAL_UNIT_US)) /* 10.24s */
			return HCI_ERR_Invalid_HCI_Command_Parameters;
		ll_adv_interval_us = (((ll_adv_params.advIntervalMin +
					ll_adv_params.advIntervalMax) /
				       2) *
				      LL_ADV_INTERVAL_UNIT_US);
		/* Don't time out */
		ll_adv_timeout_us = -1;
		break;
	case BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND:
		ll_adv_interval_us = LL_ADV_DIRECT_INTERVAL_US;
		ll_adv_timeout_us = LL_ADV_DIRECT_TIMEOUT_US;
		break;
	default:
		return HCI_ERR_Invalid_HCI_Command_Parameters;
	}

	/* Initialize the ADV PDU */
	ll_adv_pdu.header_type_adv = 1;
	ll_adv_pdu.header.adv.type = ll_adv_params.advType;
	ll_adv_pdu.header.adv.txaddr = ll_adv_params.useRandomAddress;

	if (ll_adv_params.useRandomAddress)
		memcpy(ll_adv_pdu.payload, &ll_random_address,
		       BLUETOOTH_ADDR_OCTETS);
	else
		memcpy(ll_adv_pdu.payload, &ll_public_address,
		       BLUETOOTH_ADDR_OCTETS);

	if (ll_adv_params.advType == BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND) {
		ll_adv_pdu.header.adv.rxaddr =
			ll_adv_params.directRandomAddress;
		memcpy(&ll_adv_pdu.payload[BLUETOOTH_ADDR_OCTETS],
		       ll_adv_params.directAddr,
		       sizeof(ll_adv_params.directAddr));
		ll_adv_pdu.header.adv.length = 12;
	} else {
		ll_adv_pdu.header.adv.rxaddr = 0;
	}

	/* All other types get data from SetAdvertisingData */

	/* Initialize the Scan Rsp PDU */
	ll_scan_rsp_pdu.header_type_adv = 1;
	ll_scan_rsp_pdu.header.adv.type = BLE_ADV_HEADER_PDU_TYPE_SCAN_RSP;
	ll_scan_rsp_pdu.header.adv.txaddr = ll_adv_params.useRandomAddress;

	if (ll_adv_params.useRandomAddress)
		memcpy(ll_scan_rsp_pdu.payload, &ll_random_address,
		       BLUETOOTH_ADDR_OCTETS);
	else
		memcpy(ll_scan_rsp_pdu.payload, &ll_public_address,
		       BLUETOOTH_ADDR_OCTETS);

	ll_scan_rsp_pdu.header.adv.rxaddr = 0;

	return HCI_SUCCESS;
}

static uint32_t tx_end, rsp_end, tx_rsp_end;
struct ble_pdu ll_rcv_packet;

/**
 * Advertises packet that has already been generated on given channel.
 *
 * This function also processes any incoming scan requests.
 *
 * @param    chan The channel on which to advertise.
 * @returns  EC_SUCCESS on packet reception, otherwise error.
 */
int ble_ll_adv(int chan)
{
	int rv;

	ble_radio_init(BLE_ADV_ACCESS_ADDRESS, BLE_ADV_CRCINIT);

	/* Change channel */
	NRF51_RADIO_FREQUENCY = NRF51_RADIO_FREQUENCY_VAL(chan2freq(chan));
	NRF51_RADIO_DATAWHITEIV = chan;

	ble_tx(&ll_adv_pdu);

	while (!RADIO_DONE)
		;

	tx_end = get_time().le.lo;

	if (ll_adv_pdu.header.adv.type ==
	    BLE_ADV_HEADER_PDU_TYPE_ADV_NONCONN_IND)
		return EC_SUCCESS;

	rv = ble_rx(&ll_rcv_packet, 16000, 1);

	if (rv != EC_SUCCESS)
		return rv;

	while (!RADIO_DONE)
		;

	tx_rsp_end = get_time().le.lo;

	/* Check for valid responses */
	switch (ll_rcv_packet.header.adv.type) {
	case BLE_ADV_HEADER_PDU_TYPE_SCAN_REQ:
		/* Scan requests are only allowed for ADV_IND and SCAN_IND */
		if ((ll_adv_pdu.header.adv.type !=
			     BLE_ADV_HEADER_PDU_TYPE_ADV_IND &&
		     ll_adv_pdu.header.adv.type !=
			     BLE_ADV_HEADER_PDU_TYPE_ADV_SCAN_IND) ||
		    /* The advertising address needs to match */
		    (memcmp(&ll_rcv_packet.payload[BLUETOOTH_ADDR_OCTETS],
			    &ll_adv_pdu.payload[0], BLUETOOTH_ADDR_OCTETS))) {
			/* Don't send the scan response */
			radio_disable();
			return rv;
		}
		break;
	case BLE_ADV_HEADER_PDU_TYPE_CONNECT_REQ:
		/* Don't send a scan response */
		radio_disable();
		/* Connecting is only allowed for ADV_IND and ADV_DIRECT_IND */
		if (ll_adv_pdu.header.adv.type !=
			    BLE_ADV_HEADER_PDU_TYPE_ADV_IND &&
		    ll_adv_pdu.header.adv.type !=
			    BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND)
			return rv;
		/* The advertising address needs to match */
		if (memcmp(&ll_rcv_packet.payload[BLUETOOTH_ADDR_OCTETS],
			   &ll_adv_pdu.payload[0], BLUETOOTH_ADDR_OCTETS))
			return rv;
		/* The InitAddr address needs to match for ADV_DIRECT_IND */
		if (ll_adv_pdu.header.adv.type ==
			    BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND &&
		    memcmp(&ll_adv_pdu.payload[BLUETOOTH_ADDR_OCTETS],
			   &ll_rcv_packet.payload[0], BLUETOOTH_ADDR_OCTETS))
			return rv;

		/* Mark time that connect was received */
		time_of_connect_req = NRF51_TIMER_CC(0, 1);

		/*
		 * Enter connection state upon receiving
		 * a connect request packet
		 */
		ll_state = CONNECTION;

		return rv;
		break;
	default: /* Unhandled response packet */
		radio_disable();
		return rv;
		break;
	}

	CPRINTF("ADV %u Response %u %u\n", tx_end, rsp_end, tx_rsp_end);

	return rv;
}

int ble_ll_adv_event(void)
{
	int chan_idx;
	int rv = EC_SUCCESS;

	for (chan_idx = 0; chan_idx < 3; chan_idx++) {
		if (ll_adv_params.advChannelMap & BIT(chan_idx)) {
			rv = ble_ll_adv(chan_idx + 37);
			if (rv != EC_SUCCESS)
				return rv;
		}
	}

	return rv;
}

void print_connection_state(void)
{
	CPRINTF("vvvvvvvvvvvvvvvvvvvCONNECTION STATEvvvvvvvvvvvvvvvvvvv\n");
	CPRINTF("Number of connections events processed: %d\n", ll_conn_events);
	CPRINTF("Recovered from %d bad receives.\n", errors_recovered);
	CPRINTF("Access addr(hex): %x\n", conn_params.access_addr);
	CPRINTF("win_size(hex): %x\n", conn_params.win_size);
	CPRINTF("win_offset(hex): %x\n", conn_params.win_offset);
	CPRINTF("interval(hex): %x\n", conn_params.interval);
	CPRINTF("latency(hex): %x\n", conn_params.latency);
	CPRINTF("timeout(hex): %x\n", conn_params.timeout);
	CPRINTF("channel_map(hex): %llx\n", conn_params.channel_map);
	CPRINTF("hop(hex): %x\n", conn_params.hop_increment);
	CPRINTF("SCA(hex): %x\n", conn_params.sleep_clock_accuracy);
	CPRINTF("transmitWindowOffset: %d\n", conn_params.transmitWindowOffset);
	CPRINTF("connInterval: %d\n", conn_params.connInterval);
	CPRINTF("transmitWindowSize: %d\n", conn_params.transmitWindowSize);
	CPRINTF("^^^^^^^^^^^^^^^^^^^CONNECTION STATE^^^^^^^^^^^^^^^^^^^\n");
}

int connected_communicate(void)
{
	int rv;
	long sleep_time;
	int offset = 0;
	uint64_t listen_time;
	uint8_t comm_channel = get_next_data_channel(&remap_table);

	if (num_consecutive_failures > 0) {
		ble_radio_init(conn_params.access_addr,
			       conn_params.crc_init_val);
		NRF51_RADIO_FREQUENCY =
			NRF51_RADIO_FREQUENCY_VAL(chan2freq(comm_channel));
		NRF51_RADIO_DATAWHITEIV = comm_channel;
		listen_time = last_receive_time + conn_params.connInterval -
			      get_time().val + conn_params.transmitWindowSize;

		/*
		 * This listens for 1.25 times the expected amount
		 * of time. This is a margin of error. This line is
		 * only called when a connection has failed (a missed
		 * packet). The peripheral and the controller could have
		 * missed this packet due to a disagreement on when
		 * the packet should have arrived. We listen for
		 * slightly longer than expected in the case that
		 * there was a timing disagreement.
		 */
		rv = ble_rx(&ll_rcv_packet, listen_time + (listen_time >> 2),
			    0);
	} else {
		if (!is_first_data_packet) {
			sleep_time = receive_time + conn_params.connInterval -
				     get_time().val;
			/*
			 * The time slept is 31/32 (96.875%) of the calculated
			 * required sleep time because the code to receive
			 * packets requires time to set up.
			 */
			crec_usleep(sleep_time - (sleep_time >> 5));
		} else {
			last_receive_time = time_of_connect_req;
			sleep_time = TRANSMIT_WINDOW_OFFSET_CONSTANT +
				     conn_params.transmitWindowOffset +
				     time_of_connect_req - get_time().val;
			if (sleep_time >= 0) {
				/*
				 * Radio is on for longer than needed for first
				 * packet to make sure that it is received.
				 */
				crec_usleep(sleep_time - (sleep_time >> 2));
			} else {
				return EC_ERROR_TIMEOUT;
			}
		}

		ble_radio_init(conn_params.access_addr,
			       conn_params.crc_init_val);
		NRF51_RADIO_FREQUENCY =
			NRF51_RADIO_FREQUENCY_VAL(chan2freq(comm_channel));
		NRF51_RADIO_DATAWHITEIV = comm_channel;

		/*
		 * Timing the transmit window is very hard to do when the code
		 * executing has actual effect on the timing. To combat this,
		 * the radio starts a little early, and terminates when the
		 * window normally should. The variable 'offset' represents
		 * how early the window opens in microseconds.
		 */
		if (!is_first_data_packet)
			offset = last_receive_time + conn_params.connInterval -
				 get_time().val;
		else
			offset = 0;

		rv = ble_rx(&ll_rcv_packet,
			    offset + conn_params.transmitWindowSize, 0);
	}

	/*
	 * The radio shortcuts have been set up so that transmission
	 * occurs automatically after receiving. The radio just needs
	 * to know where to find the packet to be sent.
	 */
	NRF51_RADIO_PACKETPTR = (uint32_t)packet_tb_sent;

	receive_time = NRF51_TIMER_CC(0, 1);
	if (rv != EC_SUCCESS)
		receive_time = last_receive_time + conn_params.connInterval;

	while (!RADIO_DONE)
		;

	last_receive_time = receive_time;
	is_first_data_packet = 0;

	return rv;
}

static uint32_t ll_adv_events;
static timestamp_t deadline;
static uint32_t start, end;

void bluetooth_ll_task(void)
{
	uint64_t last_rx_time = 0;
	CPRINTS("LL task init");

	while (1) {
		switch (ll_state) {
		case ADVERTISING:

			if (deadline.val == 0) {
				CPRINTS("ADV @%p", &ll_adv_pdu);
				deadline.val = get_time().val +
					       (uint32_t)ll_adv_timeout_us;
				ll_adv_events = 0;
			}

			ble_ll_adv_event();
			ll_adv_events++;

			if (ll_state == CONNECTION) {
				receive_time = 0;
				break;
			}
			/* sleep for 0-10ms */
			crec_usleep(ll_adv_interval_us + ll_pseudo_rand(10000));

			if (get_time().val > deadline.val) {
				ll_state = STANDBY;
				break;
			}
			break;
		case STANDBY:
			deadline.val = 0;
			CPRINTS("Standby %d events", ll_adv_events);
			ll_adv_events = 0;
			ll_conn_events = 0;
			task_wait_event(-1);
			connection_initialized = 0;
			errors_recovered = 0;
			break;
		case TEST_RX:
			if (ble_test_rx() == HCI_SUCCESS)
				ll_test_packets++;
			/* Packets come every 625us, sleep to save power */
			crec_usleep(300);
			break;
		case TEST_TX:
			start = get_time().le.lo;
			ble_test_tx();
			ll_test_packets++;
			end = get_time().le.lo;
			crec_usleep(625 - 82 - (end - start)); /* 625us */
			break;
		case UNINITIALIZED:
			ble_radio_init(BLE_ADV_ACCESS_ADDRESS, BLE_ADV_CRCINIT);
			ll_adv_events = 0;
			task_wait_event(-1);
			connection_initialized = 0;
			packet_tb_sent = &tx_packet_1;
			set_empty_data_packet(&tx_packet_1);
			break;
		case CONNECTION:
			if (!connection_initialized) {
				if (initialize_connection() != HCI_SUCCESS) {
					ll_state = STANDBY;
					break;
				}
				connection_initialized = 1;
				last_rx_time = NRF51_TIMER_CC(0, 1);
			}

			if (connected_communicate() == EC_SUCCESS) {
				if (num_consecutive_failures > 0)
					++errors_recovered;
				num_consecutive_failures = 0;
				last_rx_time = get_time().val;
			} else {
				num_consecutive_failures++;
				if ((get_time().val - last_rx_time) >
				    conn_params.connSupervisionTimeout) {
					ll_state = STANDBY;
					CPRINTF("EXITING CONNECTION STATE "
						"DUE TO TIMEOUT.\n");
				}
			}
			++ll_conn_events;

			if (ll_state == STANDBY) {
				CPRINTF("Exiting connection state/Entering "
					"Standby state after %d connections "
					"events\n",
					ll_conn_events);
				print_connection_state();
			}
			break;
		default:
			CPRINTS("Unhandled State ll_state = %d", ll_state);
			ll_state = UNINITIALIZED;
			task_wait_event(-1);
		}
	}
}
