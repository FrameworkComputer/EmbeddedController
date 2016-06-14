/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bluetooth_le_ll.h"
#include "bluetooth_le.h"
#include "btle_hci_int.h"
#include "util.h"
#include "console.h"
#include "radio.h"
#include "radio_test.h"
#include "task.h"
#include "timer.h"

#ifdef CONFIG_BLUETOOTH_LL_DEBUG

#define CPUTS(outstr) cputs(CC_BLUETOOTH_LL, outstr)
#define CPRINTS(format, args...) cprints(CC_BLUETOOTH_LL, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_BLUETOOTH_LL, format, ## args)

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

int ll_power;

static uint64_t ll_random_address = 0xC5BADBADBAD1; /* Uninitialized */
static uint64_t ll_public_address = 0xC5BADBADBADF; /* Uninitialized */
static uint8_t ll_channel_map[5] = {0xff, 0xff, 0xff, 0xff, 0x1f};

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

	ble_radio_clear_white_list();

	return HCI_SUCCESS;
}

static uint8_t ll_state_change_request(enum ll_state_t next_state)
{
	/* Initialize the radio if it hasn't been initialized */
	if (ll_state == UNINITIALIZED) {
		if (ble_radio_init() != EC_SUCCESS)
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

/* White List */
uint8_t ll_clear_white_list(void)
{
	if (ble_radio_clear_white_list() == EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Hardware_Failure;
}

uint8_t ll_read_white_list_size(uint8_t *return_params)
{
	if (ble_radio_read_white_list_size(return_params) == EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Hardware_Failure;
}

uint8_t ll_add_device_to_white_list(uint8_t *params)
{
	if (ble_radio_add_device_to_white_list(&params[1], params[0]) ==
			EC_SUCCESS)
		return HCI_SUCCESS;
	else
		return HCI_ERR_Host_Rejected_Due_To_Limited_Resources;
}

uint8_t ll_remove_device_from_white_list(uint8_t *params)
{
	if (ble_radio_remove_device_from_white_list(&params[1], params[0]) ==
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
		    (100000 / LL_ADV_INTERVAL_UNIT_US))    /* 100ms */
			return HCI_ERR_Invalid_HCI_Command_Parameters;
	/* Fall through */
	case BLE_ADV_HEADER_PDU_TYPE_ADV_IND:
		if (ll_adv_params.advIntervalMin > ll_adv_params.advIntervalMax)
			return HCI_ERR_Invalid_HCI_Command_Parameters;
		if (ll_adv_params.advIntervalMin <
		    (20000 / LL_ADV_INTERVAL_UNIT_US) ||   /* 20ms */
		    ll_adv_params.advIntervalMax >
		    (10240000 / LL_ADV_INTERVAL_UNIT_US))  /* 10.24s */
			return HCI_ERR_Invalid_HCI_Command_Parameters;
		ll_adv_interval_us = (((ll_adv_params.advIntervalMin +
					ll_adv_params.advIntervalMax) / 2) *
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
	/* Change channel */
	NRF51_RADIO_FREQUENCY = NRF51_RADIO_FREQUENCY_VAL(chan2freq(chan));
	NRF51_RADIO_DATAWHITEIV = chan;

	ble_tx(&ll_adv_pdu);

	while (!RADIO_DONE)
		;

	tx_end = get_time().le.lo;

	if (ll_adv_pdu.header.adv.type ==
	    BLE_ADV_HEADER_PDU_TYPE_ADV_NONCONN_IND)
		return rv;

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
	break;
	default: /* Unhandled response packet */
		radio_disable();
		return rv;
	break;
	}

	dump_ble_packet(&ll_rcv_packet);
	CPRINTF("ADV %u Response %u %u\n", tx_end, rsp_end, tx_rsp_end);

	return rv;
}

int ble_ll_adv_event(void)
{
	int chan_idx;
	int rv;

	for (chan_idx = 0; chan_idx < 3; chan_idx++) {
		if (ll_adv_params.advChannelMap & (1 << chan_idx)) {
			rv = ble_ll_adv(chan_idx + 37);
			if (rv != EC_SUCCESS)
				return rv;
		}
	}

	return rv;
}

static int ll_adv_events;
static timestamp_t deadline;
static uint32_t start, end;

void bluetooth_ll_task(void)
{
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

			/* sleep for 0-10ms */
			usleep(ll_adv_interval_us + ll_pseudo_rand(10000));

			if (get_time().val > deadline.val) {
				ll_state = STANDBY;
				break;
			}
		break;
		case STANDBY:
			deadline.val = 0;
			CPRINTS("Standby %d events", ll_adv_events);
			ll_adv_events = 0;
			task_wait_event(-1);
		break;
		case TEST_RX:
			if (ble_test_rx() == HCI_SUCCESS)
				ll_test_packets++;
			/* Packets come every 625us, sleep to save power */
			usleep(300);
		break;
		case TEST_TX:
			start = get_time().le.lo;
			ble_test_tx();
			ll_test_packets++;
			end = get_time().le.lo;
			usleep(625 - 82 - (end-start)); /* 625us */
		break;
		case UNINITIALIZED:
			ll_adv_events = 0;
			task_wait_event(-1);
		break;
		default:
			CPRINTS("Unhandled State ll_state = %d", ll_state);
			ll_state = UNINITIALIZED;
			task_wait_event(-1);
		}
	}
}

