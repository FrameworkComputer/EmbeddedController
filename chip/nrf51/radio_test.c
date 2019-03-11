/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bluetooth_le.h" /* chan2freq */
#include "btle_hci_int.h"
#include "console.h"
#include "radio.h"
#include "radio_test.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

#define BLE_TEST_TYPE_PRBS9   0
#define BLE_TEST_TYPE_F0      1
#define BLE_TEST_TYPE_AA      2
#define BLE_TEST_TYPE_PRBS15  3
#define BLE_TEST_TYPE_FF      4
#define BLE_TEST_TYPE_00      5
#define BLE_TEST_TYPE_0F      6
#define BLE_TEST_TYPE_55      7

#define BLE_TEST_TYPES_IMPLEMENTED 0xf6 /* No PRBS yet */

static struct nrf51_ble_packet_t rx_packet;
static struct nrf51_ble_packet_t tx_packet;
static uint32_t rx_end;

static int test_in_progress;

void ble_test_stop(void)
{
	test_in_progress = 0;
}

static uint32_t prbs_lfsr;
static uint32_t prbs_poly;

/*
 * This is a Galois LFSR, the polynomial is the counterpart of the Fibonacci
 * LFSR in the doc.  It requires fewer XORs to implement in software.
 * This also means that the initial value is different.
 */
static uint8_t prbs_next_byte(void)
{
	int i;
	int lsb;
	uint8_t rv = 0;

	for (i = 0; i < 8; i++) {
		lsb = prbs_lfsr & 1;
		rv |= lsb << i;
		prbs_lfsr = prbs_lfsr >> 1;
		if (lsb)
			prbs_lfsr ^= prbs_poly;
	}
	return rv;
}

void ble_test_fill_tx_packet(int type, int len)
{
	int i;

	tx_packet.s0 = type & 0xf;
	tx_packet.length = len;

	switch (type) {
	case BLE_TEST_TYPE_PRBS9:
		prbs_lfsr = 0xf;
		prbs_poly = 0x108;
		for (i = 0; i < len; i++)
			tx_packet.payload[i] = prbs_next_byte();
		break;
	case BLE_TEST_TYPE_PRBS15:
		prbs_lfsr = 0xf;
		prbs_poly = 0x6000;
		for (i = 0; i < len; i++)
			tx_packet.payload[i] = prbs_next_byte();
		break;
	case BLE_TEST_TYPE_F0:
		memset(tx_packet.payload, 0xF0, len);
		break;
	case BLE_TEST_TYPE_AA:
		memset(tx_packet.payload, 0xAA, len);
		break;
	case BLE_TEST_TYPE_FF:
		memset(tx_packet.payload, 0xFF, len);
		break;
	case BLE_TEST_TYPE_00:
		memset(tx_packet.payload, 0x00, len);
		break;
	case BLE_TEST_TYPE_0F:
		memset(tx_packet.payload, 0x0F, len);
		break;
	case BLE_TEST_TYPE_55:
		memset(tx_packet.payload, 0x55, len);
		break;
	default:
		break;
	}
}

static int ble_test_init(int chan)
{
	int rv = radio_init(BLE_1MBIT);

	if (rv)
		return HCI_ERR_Hardware_Failure;

	if (chan > BLE_MAX_TEST_CHANNEL || chan < BLE_MIN_TEST_CHANNEL)
		return HCI_ERR_Invalid_HCI_Command_Parameters;

	NRF51_RADIO_CRCCNF = 3 | BIT(8); /* 3-byte, skip address */
	/* x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1 */
	/* 0x1_0000_0000_0000_0110_0101_1011 */
	NRF51_RADIO_CRCPOLY = 0x100065B;
	NRF51_RADIO_CRCINIT = 0x555555;

	NRF51_RADIO_TXPOWER = NRF51_RADIO_TXPOWER_0_DBM;

	/* The testing address is the inverse of the advertising address. */
	NRF51_RADIO_BASE0 = (~BLE_ADV_ACCESS_ADDRESS) << 8;

	NRF51_RADIO_PREFIX0 = (~BLE_ADV_ACCESS_ADDRESS) >> 24;

	NRF51_RADIO_TXADDRESS = 0;
	NRF51_RADIO_RXADDRESSES = 1;

	NRF51_RADIO_PCNF0 = NRF51_RADIO_PCNF0_TEST;

	NRF51_RADIO_PCNF1 = NRF51_RADIO_PCNF1_TEST;

	NRF51_RADIO_FREQUENCY = NRF51_RADIO_FREQUENCY_VAL(2*chan + 2402);

	test_in_progress = 1;
	return rv;
}

int ble_test_rx_init(int chan)
{
	NRF51_RADIO_PACKETPTR = (uint32_t)&rx_packet;
	return ble_test_init(chan);
}

int ble_test_tx_init(int chan, int len, int type)
{
	if ((BIT(type) & BLE_TEST_TYPES_IMPLEMENTED) == 0 ||
			(len < 0 || len > BLE_MAX_TEST_PAYLOAD_OCTETS))
		return HCI_ERR_Invalid_HCI_Command_Parameters;

	ble_test_fill_tx_packet(type, len);
	NRF51_RADIO_PACKETPTR = (uint32_t)&tx_packet;

	return ble_test_init(chan);
}

void ble_test_tx(void)
{
	NRF51_RADIO_END = 0;
	NRF51_RADIO_TXEN = 1;
}

int ble_test_rx(void)
{
	int retries = 100;

	NRF51_RADIO_END = 0;
	NRF51_RADIO_RXEN = 1;

	do {
		retries--;
		if (retries <= 0) {
			radio_disable();
			return EC_ERROR_TIMEOUT;
		}
		usleep(100);
	} while (!NRF51_RADIO_END);

	rx_end = get_time().le.lo;

	return EC_SUCCESS;
}

