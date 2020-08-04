/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bluetooth_le.h"
#include "include/bluetooth_le.h"
#include "console.h"
#include "ppi.h"
#include "radio.h"
#include "registers.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_BLUETOOTH_LE, outstr)
#define CPRINTS(format, args...) cprints(CC_BLUETOOTH_LE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_BLUETOOTH_LE, format, ## args)

static void ble2nrf_packet(struct ble_pdu *ble_p,
		struct nrf51_ble_packet_t *radio_p)
{
	if (ble_p->header_type_adv) {
		radio_p->s0 = ble_p->header.adv.type & 0xf;
		radio_p->s0 |= (ble_p->header.adv.txaddr ?
			1 << BLE_ADV_HEADER_TXADD_SHIFT : 0);
		radio_p->s0 |= (ble_p->header.adv.rxaddr ?
			1 << BLE_ADV_HEADER_RXADD_SHIFT : 0);
		radio_p->length = ble_p->header.adv.length & 0x3f; /* 6 bits */
	} else {
		radio_p->s0 = ble_p->header.data.llid & 0x3;
		radio_p->s0 |= (ble_p->header.data.nesn ?
			1 << BLE_DATA_HEADER_NESN_SHIFT : 0);
		radio_p->s0 |= (ble_p->header.data.sn ?
			1 << BLE_DATA_HEADER_SN_SHIFT : 0);
		radio_p->s0 |= (ble_p->header.data.md ?
			1 << BLE_DATA_HEADER_MD_SHIFT : 0);
		radio_p->length = ble_p->header.data.length & 0x1f; /* 5 bits */
	}

	if (radio_p->length > 0)
		memcpy(radio_p->payload, ble_p->payload, radio_p->length);
}

static void nrf2ble_packet(struct ble_pdu *ble_p,
		struct nrf51_ble_packet_t *radio_p, int type_adv)
{
	if (type_adv) {
		ble_p->header_type_adv = 1;
		ble_p->header.adv.type = radio_p->s0 & 0xf;
		ble_p->header.adv.txaddr = (radio_p->s0 &
			BIT(BLE_ADV_HEADER_TXADD_SHIFT)) != 0;
		ble_p->header.adv.rxaddr = (radio_p->s0 &
			BIT(BLE_ADV_HEADER_RXADD_SHIFT)) != 0;
		/* Length check? 6-37 Bytes */
		ble_p->header.adv.length = radio_p->length;
	} else {
		ble_p->header_type_adv = 0;
		ble_p->header.data.llid = radio_p->s0 & 0x3;
		ble_p->header.data.nesn = (radio_p->s0 &
			BIT(BLE_DATA_HEADER_NESN_SHIFT)) != 0;
		ble_p->header.data.sn = (radio_p->s0 &
			BIT(BLE_DATA_HEADER_SN_SHIFT)) != 0;
		ble_p->header.data.md = (radio_p->s0 &
			BIT(BLE_DATA_HEADER_MD_SHIFT)) != 0;
		/* Length check? 0-31 Bytes */
		ble_p->header.data.length = radio_p->length;
	}

	if (radio_p->length > 0)
		memcpy(ble_p->payload, radio_p->payload, radio_p->length);
}

struct ble_pdu adv_packet;
struct nrf51_ble_packet_t on_air_packet;

struct ble_pdu rcv_packet;

int ble_radio_init(uint32_t access_address, uint32_t crc_init_val)
{
	int rv = radio_init(BLE_1MBIT);

	if (rv)
		return rv;
	NRF51_RADIO_CRCCNF = 3 | NRF51_RADIO_CRCCNF_SKIP_ADDR; /* 3-byte CRC */
	/* x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1 */
	/* 0x1_0000_0000_0000_0110_0101_1011 */
	NRF51_RADIO_CRCPOLY = 0x100065B;

	NRF51_RADIO_CRCINIT = crc_init_val;

	NRF51_RADIO_TXPOWER = NRF51_RADIO_TXPOWER_0_DBM;

	NRF51_RADIO_BASE0 = access_address << 8;
	NRF51_RADIO_PREFIX0 = access_address >> 24;

	if (access_address != BLE_ADV_ACCESS_ADDRESS)
		CPRINTF("Initializing radio for data packet.\n");

	NRF51_RADIO_TXADDRESS = 0;
	NRF51_RADIO_RXADDRESSES = 1;
	NRF51_RADIO_PCNF0 = NRF51_RADIO_PCNF0_ADV_DATA;
	NRF51_RADIO_PCNF1 = NRF51_RADIO_PCNF1_ADV_DATA;

	return rv;

}

static struct nrf51_ble_packet_t tx_packet;

static uint32_t tx_end, rsp_end;

void ble_tx(struct ble_pdu *pdu)
{
	uint32_t timeout_time;

	ble2nrf_packet(pdu, &tx_packet);

	NRF51_RADIO_PACKETPTR = (uint32_t)&tx_packet;
	NRF51_RADIO_END = NRF51_RADIO_PAYLOAD = NRF51_RADIO_ADDRESS = 0;
	NRF51_RADIO_RXEN = 0;
	NRF51_RADIO_TXEN = 1;

	timeout_time = get_time().val + RADIO_SETUP_TIMEOUT;
	while (!NRF51_RADIO_READY) {
		if (get_time().val > timeout_time) {
			CPRINTF("ERROR DURING RADIO TX SETUP. TRY AGAIN.\n");
			return;
		}
	}

	timeout_time = get_time().val + RADIO_SETUP_TIMEOUT;
	while (!NRF51_RADIO_END) {
		if (get_time().val > timeout_time) {
			CPRINTF("RADIO DID NOT SHUT DOWN AFTER TX. "
				"RECOMMEND REBOOT.\n");
			return;
		}
	}
	NRF51_RADIO_DISABLE = 1;
}

static struct nrf51_ble_packet_t rx_packet;
int ble_rx(struct ble_pdu *pdu, int timeout, int adv)
{
	uint32_t done;
	uint32_t timeout_time;
	int ppi_channel_requested;

	/* Prevent illegal wait times */
	if (timeout <= 0) {
		NRF51_RADIO_DISABLE = 1;
		return EC_ERROR_TIMEOUT;
	}

	NRF51_RADIO_PACKETPTR = (uint32_t)&rx_packet;
	NRF51_RADIO_END = NRF51_RADIO_PAYLOAD = NRF51_RADIO_ADDRESS = 0;
	/*
	 * These shortcuts cause packet transmission 150 microseconds after
	 * packet receive, as is the BTLE standard. See NRF51 manual:
	 * section 17.1.12
	 */
	NRF51_RADIO_SHORTS = NRF51_RADIO_SHORTS_READY_START |
			NRF51_RADIO_SHORTS_DISABLED_TXEN |
			NRF51_RADIO_SHORTS_END_DISABLE;

	/*
	 * This creates a shortcut that marks the time
	 * that the payload was received by the radio
	 * in NRF51_TIMER_CC(0,1)
	 */
	ppi_channel_requested = NRF51_PPI_CH_RADIO_ADDR__TIMER0CC1;
	if (ppi_request_channel(&ppi_channel_requested) == EC_SUCCESS) {
		NRF51_PPI_CHEN |= BIT(ppi_channel_requested);
		NRF51_PPI_CHENSET |= BIT(ppi_channel_requested);
	}


	NRF51_RADIO_RXEN = 1;

	timeout_time = get_time().val + RADIO_SETUP_TIMEOUT;
	while (!NRF51_RADIO_READY) {
		if (get_time().val > timeout_time) {
			CPRINTF("RADIO NOT SET UP IN TIME. TIMING OUT.\n");
			return EC_ERROR_TIMEOUT;
		}
	}

	timeout_time = get_time().val + timeout;
	do {
		if (get_time().val >= timeout_time) {
			NRF51_RADIO_DISABLE = 1;
			return EC_ERROR_TIMEOUT;
		}
		done = NRF51_RADIO_END;
	} while (!done);

	rsp_end = get_time().le.lo;

	if (NRF51_RADIO_CRCSTATUS == 0) {
		CPRINTF("INVALID CRC\n");
		return EC_ERROR_CRC;
	}

	nrf2ble_packet(pdu, &rx_packet, adv);

	/*
	 * Throw error if radio not yet disabled. Something has
	 * gone wrong. May be in an unexpected state.
	 */
	if (NRF51_RADIO_DISABLED != 1)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/* Allow list handling */
int ble_radio_clear_allow_list(void)
{
	NRF51_RADIO_DACNF = 0;
	return EC_SUCCESS;
}

int ble_radio_read_allow_list_size(uint8_t *ret_size)
{
	int i, size = 0;
	uint32_t dacnf = NRF51_RADIO_DACNF;

	/* Count the bits that are set */
	for (i = 0; i < NRF51_RADIO_DACNF_MAX; i++)
		if (dacnf & NRF51_RADIO_DACNF_ENA(i))
			size++;

	*ret_size = size;

	return EC_SUCCESS;
}

int ble_radio_add_device_to_allow_list(const uint8_t *addr_ptr, uint8_t rand)
{
	uint32_t dacnf = NRF51_RADIO_DACNF;
	int i;
	uint32_t aligned;

	/* Check for duplicates using ble_radio_remove_device? */

	/* Find a free entry */
	for (i = 0; i < NRF51_RADIO_DACNF_MAX &&
		(dacnf & NRF51_RADIO_DACNF_ENA(i)); i++)
		;

	if (i == NRF51_RADIO_DACNF_MAX)
		return EC_ERROR_OVERFLOW;

	memcpy(&aligned, addr_ptr, 4);
	NRF51_RADIO_DAB(i) = aligned;
	memcpy(&aligned, addr_ptr + 4, 2);
	NRF51_RADIO_DAP(i) = aligned;

	NRF51_RADIO_DACNF = dacnf | NRF51_RADIO_DACNF_ENA(i) |
		(rand ? NRF51_RADIO_DACNF_TXADD(i) : 0);

	return EC_SUCCESS;
}

int ble_radio_remove_device_from_allow_list(const uint8_t *addr_ptr,
					    uint8_t rand)
{
	int i, dacnf = NRF51_RADIO_DACNF;

	/* Find a matching entry */
	for (i = 0; i < NRF51_RADIO_DACNF_MAX; i++) {
		uint32_t dab = NRF51_RADIO_DAB(i), dap = NRF51_RADIO_DAP(i);

		if ((dacnf & NRF51_RADIO_DACNF_ENA(i)) && /* Enabled */
		    /* Rand flag matches */
		    (rand == ((dacnf & NRF51_RADIO_DACNF_TXADD(i)) != 0)) &&
		    /* Address matches */
		    (!memcmp(addr_ptr, &dab, 4)) &&
		    (!memcmp(addr_ptr + 4, &dap, 2)))
			break;
	}

	if (i == NRF51_RADIO_DACNF_MAX) /* Not found is successfully removed */
		return EC_SUCCESS;

	NRF51_RADIO_DACNF = dacnf & ~((NRF51_RADIO_DACNF_ENA(i)) |
		(rand ? NRF51_RADIO_DACNF_TXADD(i) : 0));

	return EC_SUCCESS;
}


int ble_adv_packet(struct ble_pdu *adv_packet, int chan)
{
	int done;
	int rv;

	/* Change channel */
	NRF51_RADIO_FREQUENCY = NRF51_RADIO_FREQUENCY_VAL(chan2freq(chan));
	NRF51_RADIO_DATAWHITEIV = chan;

	ble_tx(adv_packet);

	do {
		done = NRF51_RADIO_END;
	} while (!done);

	tx_end = get_time().le.lo;

	if (adv_packet->header.adv.type ==
	    BLE_ADV_HEADER_PDU_TYPE_ADV_NONCONN_IND)
		return EC_SUCCESS;

	rv = ble_rx(&rcv_packet, 16000, 1);

	if (rv != EC_SUCCESS)
		return rv;

	/* Check for valid responses */
	switch (rcv_packet.header.adv.type) {
	case BLE_ADV_HEADER_PDU_TYPE_SCAN_REQ:
		/* Scan requests are only allowed for ADV_IND and SCAN_IND */
		if (adv_packet->header.adv.type !=
		     BLE_ADV_HEADER_PDU_TYPE_ADV_IND &&
		    adv_packet->header.adv.type !=
		     BLE_ADV_HEADER_PDU_TYPE_ADV_SCAN_IND)
			return rv;
		/* The advertising address needs to match */
		if (memcmp(&rcv_packet.payload[BLUETOOTH_ADDR_OCTETS],
		    &adv_packet->payload[0], BLUETOOTH_ADDR_OCTETS))
			return rv;
	break;
	case BLE_ADV_HEADER_PDU_TYPE_CONNECT_REQ:
		/* Connections are only allowed for two types of advertising */
		if (adv_packet->header.adv.type !=
		     BLE_ADV_HEADER_PDU_TYPE_ADV_IND &&
		    adv_packet->header.adv.type !=
		     BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND)
			return rv;
		/* The advertising address needs to match */
		if (memcmp(&rcv_packet.payload[BLUETOOTH_ADDR_OCTETS],
		    &adv_packet->payload[0], BLUETOOTH_ADDR_OCTETS))
			return rv;
		/* The InitAddr needs to match for Directed advertising */
		if (adv_packet->header.adv.type ==
		     BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND &&
		    memcmp(&adv_packet->payload[BLUETOOTH_ADDR_OCTETS],
			   &rcv_packet.payload[0], BLUETOOTH_ADDR_OCTETS))
			return rv;
	break;
	default: /* Unhandled response packet */
		return rv;
	break;
	}

	dump_ble_packet(&rcv_packet);
	CPRINTF("tx_end %u Response %u\n", tx_end, rsp_end);

	return rv;
}

int ble_adv_event(struct ble_pdu *adv_packet)
{
	int chan;
	int rv;

	for (chan = 37; chan < 40; chan++) {
		rv = ble_adv_packet(adv_packet, chan);
		if (rv != EC_SUCCESS)
			return rv;
	}

	return rv;
}

static void fill_header(struct ble_pdu *adv, int type, int txaddr, int rxaddr)
{
	adv->header_type_adv = 1;
	adv->header.adv.type = type;
	adv->header.adv.txaddr = txaddr ?
		BLE_ADV_HEADER_RANDOM_ADDR : BLE_ADV_HEADER_PUBLIC_ADDR;
	adv->header.adv.rxaddr = rxaddr ?
		BLE_ADV_HEADER_RANDOM_ADDR : BLE_ADV_HEADER_PUBLIC_ADDR;
	adv->header.adv.length = 0;
}

static int fill_payload(uint8_t *payload, uint64_t addr, int name_length)
{
	uint8_t *curr;

	curr = pack_adv_addr(payload, addr);
	curr = pack_adv(curr, name_length, GAP_COMPLETE_NAME,
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrs");
	curr = pack_adv_int(curr, 2, GAP_APPEARANCE,
			    GAP_APPEARANCE_HID_KEYBOARD);
	curr = pack_adv_int(curr, 1, GAP_FLAGS,
		GAP_FLAGS_LE_LIM_DISC | GAP_FLAGS_LE_NO_BR_EDR);
	curr = pack_adv_int(curr, 2, GAP_COMP_16_BIT_UUID,
			    GATT_SERVICE_HID_UUID);

	return curr - payload;
}

static void fill_packet(struct ble_pdu *adv, uint64_t addr, int type,
			int name_length)
{
	fill_header(adv, type, BLE_ADV_HEADER_RANDOM_ADDR,
		    BLE_ADV_HEADER_PUBLIC_ADDR);

	adv->header.adv.length = fill_payload(adv->payload, addr, name_length);
}

static int command_ble_adv(int argc, char **argv)
{
	int type, length, reps, interval;
	uint64_t addr;
	char *e;
	int i;
	int rv;

	if (argc < 3 || argc > 5)
		return EC_ERROR_PARAM_COUNT;

	type = strtoi(argv[1], &e, 0);
	if (*e || type < 0 || (type > 2 && type != 6))
		return EC_ERROR_PARAM1;

	length = strtoi(argv[2], &e, 0);
	if (*e || length > 32)
		return EC_ERROR_PARAM2;

	if (argc >= 4) {
		reps = strtoi(argv[3], &e, 0);
		if (*e || reps < 0)
			return EC_ERROR_PARAM3;
	} else {
		reps = 1;
	}

	if (argc >= 5) {
		interval = strtoi(argv[4], &e, 0);
		if (*e || interval < 0)
			return EC_ERROR_PARAM4;
	} else {
		interval = 100000;
	}

	if (type == BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND && length != 12) {
		length = 12;
		CPRINTS("type DIRECT needs to have a length of 12");
	}

	rv = ble_radio_init(BLE_ADV_ACCESS_ADDRESS, BLE_ADV_CRCINIT);


		CPRINTS("ADV @%pP", &adv_packet);

	((uint32_t *)&addr)[0] = 0xA3A2A1A0 | type;
	((uint32_t *)&addr)[1] = BLE_RANDOM_ADDR_MSBS_STATIC << 8 | 0x5A4;

		fill_packet(&adv_packet, addr, type, length);

	for (i = 0; i < reps; i++) {
		ble_adv_event(&adv_packet);
		usleep(interval);
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(ble_adv, command_ble_adv,
			"type len [reps] [interval = 100000 (100ms)]",
			"Send a BLE packet of type type of length len");

static int command_ble_adv_scan(int argc, char **argv)
{
	int chan, packets, i;
	int addr_lsbyte;
	char *e;
	int rv;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	chan = strtoi(argv[1], &e, 0);
	if (*e || chan < 37 || chan > 39)
		return EC_ERROR_PARAM1;

	chan = strtoi(argv[1], &e, 0);
	if (*e || chan < 37 || chan > 39)
		return EC_ERROR_PARAM1;

	if (argc >= 3) {
		packets = strtoi(argv[2], &e, 0);
		if (*e || packets < 0)
			return EC_ERROR_PARAM2;
	} else {
		packets = 1;
	}

	if (argc >= 4) {
		addr_lsbyte = strtoi(argv[3], &e, 0);
		if (*e || addr_lsbyte > 255)
			return EC_ERROR_PARAM3;
	} else {
		addr_lsbyte = -1;
	}

	rv = ble_radio_init(BLE_ADV_ACCESS_ADDRESS, BLE_ADV_CRCINIT);

	/* Change channel */
	NRF51_RADIO_FREQUENCY = NRF51_RADIO_FREQUENCY_VAL(chan2freq(chan));
	NRF51_RADIO_DATAWHITEIV = chan;

	CPRINTS("ADV Listen");
	if (addr_lsbyte != -1)
		CPRINTS("filtered (%x)", addr_lsbyte);

	for (i = 0; i < packets; i++) {
		rv = ble_rx(&rcv_packet, 1000000, 1);

		if (rv == EC_ERROR_TIMEOUT)
			continue;

		if (addr_lsbyte == -1 || rcv_packet.payload[0] == addr_lsbyte)
			dump_ble_packet(&rcv_packet);
	}

	rv = radio_disable();

	CPRINTS("on_air payload rcvd %pP", &rx_packet);

	return rv;
}
DECLARE_CONSOLE_COMMAND(ble_scan, command_ble_adv_scan,
			"chan [num] [addr0]",
			"Scan for [num] BLE packets on channel chan");

