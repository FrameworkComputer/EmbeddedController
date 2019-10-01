/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bluetooth_le.h"
#include "util.h"
#include "console.h"

#define CPRINTF(format, args...) cprintf(CC_BLUETOOTH_LE, format, ## args)

/*
 * Convert from BLE Channel to frequency
 *
 * Bluetooth 4.1 Vol 6 pg 36 4.1 Table 1.1
 */

#define CHAN_0_MHZ      2404
#define CHAN_11_MHZ     2428
#define CHAN_37_MHZ     2402
#define CHAN_38_MHZ     2426
#define CHAN_39_MHZ     2480

int chan2freq(int channel)
{
	int freq;

	ASSERT(channel < 40 && channel >= 0);

	switch (channel) {
	case 37: /* Advertising */
		freq = CHAN_37_MHZ;
		break;
	case 38: /* Advertising */
		freq = CHAN_38_MHZ;
		break;
	case 39: /* Advertising */
		freq = CHAN_39_MHZ;
		break;
	default:
		/* Data Channels */
		if (channel < 11)
			freq = channel * 2 + CHAN_0_MHZ;
		else
			freq = (channel - 11) * 2 + CHAN_11_MHZ;
	}
	return freq;
}

/* BLE 4.1 Vol 6 2.3.3.1 */

void fill_remapping_table(struct remapping_table *rt, uint8_t map[5],
			  int hop_increment)
{
	int i;

	rt->num_used_channels = 0;
	rt->last_unmapped_channel = 0;
	rt->hop_increment = hop_increment;

	for (i = 0; i < 37; i++)
		if (map[i / 8] & (1 << (i % 8)))
			rt->remapping_index[rt->num_used_channels++] = i;
	memcpy(rt->map, map, sizeof(rt->map));
}

/* BLE 4.1 Vol 6 4.5.8 */
uint8_t get_next_data_channel(struct remapping_table *rt)
{
	rt->last_unmapped_channel =
		(rt->last_unmapped_channel + rt->hop_increment) % 37;

	/* Check if the channel is mapped */
	if (rt->map[rt->last_unmapped_channel / 8] &
			(1 << (rt->last_unmapped_channel % 8)))
		return rt->last_unmapped_channel;
	else
		return rt->remapping_index
			[rt->last_unmapped_channel % rt->num_used_channels];
}

/* BLE 4.1 Vol 3 Part C 11 */

/* Pack advertising structures for sending */
uint8_t *pack_adv(uint8_t *dest, int length, int type, const uint8_t *data)
{
	/* Add the structure length */
	dest[0] = (uint8_t)length+1;
	/* Add the structure type */
	dest[1] = (uint8_t)type;
	/* Add the data */
	memcpy(&dest[2], data, length);

	/* Return a pointer to the next structure */
	return &dest[2+length];
}

uint8_t *pack_adv_int(uint8_t *dest, int length, int type, int data)
{
	/* Add the structure length */
	dest[0] = (uint8_t)length+1;
	/* Add the structure type */
	dest[1] = (uint8_t)type;
	/* Add the data */
	memcpy(&dest[2], &data, length);

	/* Return a pointer to the next structure */
	return &dest[2+length];
}

uint8_t *pack_adv_addr(uint8_t *dest, uint64_t addr)
{
	memcpy(&dest[0], &addr, BLUETOOTH_ADDR_OCTETS);

	/* Return a pointer to the next structure */
	return &dest[BLUETOOTH_ADDR_OCTETS];
}

/* Parse advertising structures that have been received */
const uint8_t *unpack_adv(const uint8_t *src, int *length, int *type,
			  const uint8_t **data)
{
	/* Get the structure length */
	*length = *(src++);
	/* Get the structure type */
	*type = *(src++);
	/* Get the data */
	*data = src;

	/* Return a pointer to the next structure */
	return src + *length;
}

static void mem_dump(uint8_t *mem, int len)
{
	int i;
	uint8_t value;

	for (i = 0; i < len; i++) {
		value = mem[i];
		if (i % 8 == 0)
			CPRINTF("\n%pP: %02x", &mem[i], value);
		else
			CPRINTF(" %02x", value);
	}
	CPRINTF("\n");
}

void dump_ble_addr(uint8_t *mem, char *name)
{
	int i;

	for (i = 5; i > 0; i--)
		CPRINTF("%02x.", mem[i]);
	CPRINTF("%02x %s\n", mem[0], name);
}

void dump_ble_packet(struct ble_pdu *ble_p)
{
	int curr_offs;

	if (ble_p->header_type_adv) {
		CPRINTF("BLE packet @ %pP: type %d, len %d, %s %s\n",
			ble_p, ble_p->header.adv.type, ble_p->header.adv.length,
			(ble_p->header.adv.txaddr ? " TXADDR" : ""),
			(ble_p->header.adv.rxaddr ? " RXADDR" : ""));

		curr_offs = 0;

		if (ble_p->header.adv.type ==
			BLE_ADV_HEADER_PDU_TYPE_SCAN_REQ) {
			dump_ble_addr(ble_p->payload, "ScanA");
			curr_offs += BLUETOOTH_ADDR_OCTETS;
		} else if (ble_p->header.adv.type ==
			BLE_ADV_HEADER_PDU_TYPE_CONNECT_REQ) {
			dump_ble_addr(ble_p->payload, "InitA");
			curr_offs += BLUETOOTH_ADDR_OCTETS;
		}
		/* All packets have AdvA */
		dump_ble_addr(ble_p->payload + curr_offs, "AdvA");
		curr_offs += BLUETOOTH_ADDR_OCTETS;

		if (ble_p->header.adv.type ==
		    BLE_ADV_HEADER_PDU_TYPE_ADV_DIRECT_IND)
			dump_ble_addr(ble_p->payload + curr_offs, "InitA");
		else
			mem_dump(ble_p->payload + curr_offs,
				 ble_p->header.adv.length - curr_offs);
	} else { /* Data PDUs */
		CPRINTF("BLE data packet @%pP: LLID %d,"
			" nesn %d, sn %d, md %d, length %d\n",
			ble_p, ble_p->header.data.llid, ble_p->header.data.nesn,
			ble_p->header.data.sn, ble_p->header.data.md,
			ble_p->header.data.length);
		mem_dump(ble_p->payload, ble_p->header.data.length);
	}
}

