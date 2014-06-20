/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "common.h"
#include "console.h"
#include "crc.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_config.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

/* dump full packet on RX error */
static int debug_dump;
#else
#define CPRINTF(format, args...)
const int debug_dump;
#endif

/* Control Message type */
enum {
	/* 0 Reserved */
	PD_CTRL_GOOD_CRC = 1,
	PD_CTRL_GOTO_MIN = 2,
	PD_CTRL_ACCEPT = 3,
	PD_CTRL_REJECT = 4,
	PD_CTRL_PING = 5,
	PD_CTRL_PS_RDY = 6,
	PD_CTRL_GET_SOURCE_CAP = 7,
	PD_CTRL_GET_SINK_CAP = 8,
	PD_CTRL_PROTOCOL_ERR = 9,
	PD_CTRL_SWAP = 10,
	/* 11 Reserved */
	PD_CTRL_WAIT = 12,
	PD_CTRL_SOFT_RESET = 13,
	/* 14-15 Reserved */
};

/* Data message type */
enum {
	/* 0 Reserved */
	PD_DATA_SOURCE_CAP = 1,
	PD_DATA_REQUEST = 2,
	PD_DATA_BIST = 3,
	PD_DATA_SINK_CAP = 4,
	/* 5-14 Reserved */
	PD_DATA_VENDOR_DEF = 15,
};

/* Protocol revision */
#define PD_REV10 0

/* BMC-supported bit : we are using the baseband variant of the protocol */
#define PD_BMC_SUPPORTED (1 << 15)

/* Port role */
#define PD_ROLE_SINK   0
#define PD_ROLE_SOURCE 1

/* build message header */
#define PD_HEADER(type, role, id, cnt) \
	((type) | (PD_REV10 << 6) | \
	 ((role) << 8) | ((id) << 9) | ((cnt) << 12) | \
	 PD_BMC_SUPPORTED)

#define PD_HEADER_CNT(header)  (((header) >> 12) & 7)
#define PD_HEADER_TYPE(header) ((header) & 0xF)
#define PD_HEADER_ID(header)   (((header) >> 9) & 7)

/* Encode 5 bits using Biphase Mark Coding */
#define BMC(x)   ((x &  1 ? 0x001 : 0x3FF) \
		^ (x &  2 ? 0x004 : 0x3FC) \
		^ (x &  4 ? 0x010 : 0x3F0) \
		^ (x &  8 ? 0x040 : 0x3C0) \
		^ (x & 16 ? 0x100 : 0x300))

/* 4b/5b + Bimark Phase encoding */
static const uint16_t bmc4b5b[] = {
/* 0 = 0000 */ BMC(0x1E) /* 11110 */,
/* 1 = 0001 */ BMC(0x09) /* 01001 */,
/* 2 = 0010 */ BMC(0x14) /* 10100 */,
/* 3 = 0011 */ BMC(0x15) /* 10101 */,
/* 4 = 0100 */ BMC(0x0A) /* 01010 */,
/* 5 = 0101 */ BMC(0x0B) /* 01011 */,
/* 6 = 0110 */ BMC(0x0E) /* 01110 */,
/* 7 = 0111 */ BMC(0x0F) /* 01111 */,
/* 8 = 1000 */ BMC(0x12) /* 10010 */,
/* 9 = 1001 */ BMC(0x13) /* 10011 */,
/* A = 1010 */ BMC(0x16) /* 10110 */,
/* B = 1011 */ BMC(0x17) /* 10111 */,
/* C = 1100 */ BMC(0x1A) /* 11010 */,
/* D = 1101 */ BMC(0x1B) /* 11011 */,
/* E = 1110 */ BMC(0x1C) /* 11100 */,
/* F = 1111 */ BMC(0x1D) /* 11101 */,
/* Sync-1      K-code       11000 Startsynch #1 */
/* Sync-2      K-code       10001 Startsynch #2 */
/* RST-1       K-code       00111 Hard Reset #1 */
/* RST-2       K-code       11001 Hard Reset #2 */
/* EOP         K-code       01101 EOP End Of Packet */
/* Reserved    Error        00000 */
/* Reserved    Error        00001 */
/* Reserved    Error        00010 */
/* Reserved    Error        00011 */
/* Reserved    Error        00100 */
/* Reserved    Error        00101 */
/* Reserved    Error        00110 */
/* Reserved    Error        01000 */
/* Reserved    Error        01100 */
/* Reserved    Error        10000 */
/* Reserved    Error        11111 */
};
#define PD_SYNC1 0x18
#define PD_SYNC2 0x11
#define PD_RST1  0x07
#define PD_RST2  0x19
#define PD_EOP   0x0D

static const uint8_t dec4b5b[] = {
/* Error    */ 0x10 /* 00000 */,
/* Error    */ 0x10 /* 00001 */,
/* Error    */ 0x10 /* 00010 */,
/* Error    */ 0x10 /* 00011 */,
/* Error    */ 0x10 /* 00100 */,
/* Error    */ 0x10 /* 00101 */,
/* Error    */ 0x10 /* 00110 */,
/* RST-1    */ 0x13 /* 00111 K-code: Hard Reset #1 */,
/* Error    */ 0x10 /* 01000 */,
/* 1 = 0001 */ 0x01 /* 01001 */,
/* 4 = 0100 */ 0x04 /* 01010 */,
/* 5 = 0101 */ 0x05 /* 01011 */,
/* Error    */ 0x10 /* 01100 */,
/* EOP      */ 0x15 /* 01101 K-code: EOP End Of Packet */,
/* 6 = 0110 */ 0x06 /* 01110 */,
/* 7 = 0111 */ 0x07 /* 01111 */,
/* Error    */ 0x10 /* 10000 */,
/* Sync-2   */ 0x12 /* 10001 K-code: Startsynch #2 */,
/* 8 = 1000 */ 0x08 /* 10010 */,
/* 9 = 1001 */ 0x09 /* 10011 */,
/* 2 = 0010 */ 0x02 /* 10100 */,
/* 3 = 0011 */ 0x03 /* 10101 */,
/* A = 1010 */ 0x0A /* 10110 */,
/* B = 1011 */ 0x0B /* 10111 */,
/* Sync-1   */ 0x11 /* 11000 K-code: Startsynch #1 */,
/* RST-2    */ 0x14 /* 11001 K-code: Hard Reset #2 */,
/* C = 1100 */ 0x0C /* 11010 */,
/* D = 1101 */ 0x0D /* 11011 */,
/* E = 1110 */ 0x0E /* 11100 */,
/* F = 1111 */ 0x0F /* 11101 */,
/* 0 = 0000 */ 0x00 /* 11110 */,
/* Error    */ 0x10 /* 11111 */,
};

/* Start of Packet sequence : three Sync-1 K-codes, then one Sync-2 K-code */
#define PD_SOP (PD_SYNC1 | (PD_SYNC1<<5) | (PD_SYNC1<<10) | (PD_SYNC2<<15))

/* Hard Reset sequence : three RST-1 K-codes, then one RST-2 K-code */
#define PD_HARD_RESET (PD_RST1 | (PD_RST1 << 5) |\
		      (PD_RST1 << 10) | (PD_RST2 << 15))

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7
#define PD_RETRY_COUNT 2
#define PD_HARD_RESET_COUNT 2
#define PD_CAPS_COUNT 50

/* Timers */
#define PD_T_SEND_SOURCE_CAP (1500*MSEC) /* between 1s and 2s */
#define PD_T_GET_SOURCE_CAP  (1500*MSEC) /* between 1s and 2s */
#define PD_T_SOURCE_ACTIVITY   (45*MSEC) /* between 40ms and 50ms */
#define PD_T_SENDER_RESPONSE   (30*MSEC) /* between 24ms and 30ms */
#define PD_T_PS_TRANSITION    (220*MSEC) /* between 200ms and 220ms */

/* Port role at startup */
#ifdef CONFIG_USB_PD_DUAL_ROLE
#define PD_ROLE_DEFAULT PD_ROLE_SINK
#else
#define PD_ROLE_DEFAULT PD_ROLE_SOURCE
#endif

/* current port role */
static uint8_t pd_role = PD_ROLE_DEFAULT;
/* 3-bit rolling message ID counter */
static uint8_t pd_message_id;
/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
static uint8_t pd_polarity;

static enum {
	PD_STATE_DISABLED,
#ifdef CONFIG_USB_PD_DUAL_ROLE
	PD_STATE_SNK_DISCONNECTED,
	PD_STATE_SNK_DISCOVERY,
	PD_STATE_SNK_REQUESTED,
	PD_STATE_SNK_TRANSITION,
	PD_STATE_SNK_READY,
#endif /* CONFIG_USB_PD_DUAL_ROLE */

	PD_STATE_SRC_DISCONNECTED,
	PD_STATE_SRC_DISCOVERY,
	PD_STATE_SRC_NEGOCIATE,
	PD_STATE_SRC_ACCEPTED,
	PD_STATE_SRC_TRANSITION,
	PD_STATE_SRC_READY,

	PD_STATE_HARD_RESET,
	PD_STATE_BIST,
} pd_task_state = PD_DEFAULT_STATE;

/* increment message ID counter */
static void inc_id(void)
{
	pd_message_id = (pd_message_id + 1) & PD_MESSAGE_ID_COUNT;
}

static inline int encode_short(void *ctxt, int off, uint16_t val16)
{
	off = pd_write_sym(ctxt, off, bmc4b5b[(val16 >> 0) & 0xF]);
	off = pd_write_sym(ctxt, off, bmc4b5b[(val16 >> 4) & 0xF]);
	off = pd_write_sym(ctxt, off, bmc4b5b[(val16 >> 8) & 0xF]);
	return pd_write_sym(ctxt, off, bmc4b5b[(val16 >> 12) & 0xF]);
}

static inline int encode_word(void *ctxt, int off, uint32_t val32)
{
	off = encode_short(ctxt, off, (val32 >> 0) & 0xFFFF);
	return encode_short(ctxt, off, (val32 >> 16) & 0xFFFF);
}

/* prepare a 4b/5b-encoded PD message to send */
static int prepare_message(void *ctxt, uint16_t header, uint8_t cnt,
			   const uint32_t *data)
{
	int off, i;
	crc32_init();
	/* 64-bit preamble */
	off = pd_write_preamble(ctxt);
	/* Start Of Packet: 3x Sync-1 + 1x Sync-2 */
	off = pd_write_sym(ctxt, off, BMC(PD_SYNC1));
	off = pd_write_sym(ctxt, off, BMC(PD_SYNC1));
	off = pd_write_sym(ctxt, off, BMC(PD_SYNC1));
	off = pd_write_sym(ctxt, off, BMC(PD_SYNC2));
	/* header */
	off = encode_short(ctxt, off, header);
	crc32_hash16(header);
	/* data payload */
	for (i = 0; i < cnt; i++) {
		off = encode_word(ctxt, off, data[i]);
		crc32_hash32(data[i]);
	}
	/* CRC */
	off = encode_word(ctxt, off, crc32_result());
	/* End Of Packet */
	off = pd_write_sym(ctxt, off, BMC(PD_EOP));
	/* Ensure that we have a final edge */
	return pd_write_last_edge(ctxt, off);
}

static int analyze_rx(uint32_t *payload);
static void analyze_rx_bist(void);

static void send_hard_reset(void *ctxt)
{
	int off;

	/* 64-bit preamble */
	off = pd_write_preamble(ctxt);
	/* Hard-Reset: 3x RST-1 + 1x RST-2 */
	off = pd_write_sym(ctxt, off, BMC(PD_RST1));
	off = pd_write_sym(ctxt, off, BMC(PD_RST1));
	off = pd_write_sym(ctxt, off, BMC(PD_RST1));
	off = pd_write_sym(ctxt, off, BMC(PD_RST2));
	/* Ensure that we have a final edge */
	off = pd_write_last_edge(ctxt, off);
	/* Transmit the packet */
	pd_start_tx(ctxt, pd_polarity, off);
	pd_tx_done(pd_polarity);
}

static int send_validate_message(void *ctxt, uint16_t header, uint8_t cnt,
				 const uint32_t *data)
{
	int r;
	static uint32_t payload[7];

	/* retry 3 times if we are not getting a valid answer */
	for (r = 0; r <= PD_RETRY_COUNT; r++) {
		int bit_len;
		uint16_t head;
		/* write the encoded packet in the transmission buffer */
		bit_len = prepare_message(ctxt, header, cnt, data);
		/* Transmit the packet */
		pd_start_tx(ctxt, pd_polarity, bit_len);
		pd_tx_done(pd_polarity);
		/* starting waiting for GoodCrc */
		pd_rx_start();
		/* read the incoming packet if any */
		head = analyze_rx(payload);
		pd_rx_complete();
		if (head > 0) { /* we got a good packet, analyze it */
			int type = PD_HEADER_TYPE(head);
			int nb = PD_HEADER_CNT(head);
			uint8_t id = PD_HEADER_ID(head);
			if (type == PD_CTRL_GOOD_CRC && nb == 0 &&
			   id == pd_message_id) {
				/* got the GoodCRC we were expecting */
				inc_id();
				/* do not catch last edges as a new packet */
				udelay(20);
				return bit_len;
			} else {
				/* CPRINTF("ERR ACK/%d %04x\n", id, head); */
			}
		}
	}
	/* we failed all the re-transmissions */
	/* TODO: try HardReset */
	CPRINTF("TX NO ACK %04x/%d\n", header, cnt);
	return -1;
}

static int send_control(void *ctxt, int type)
{
	int bit_len;
	uint16_t header = PD_HEADER(type, pd_role, pd_message_id, 0);

	bit_len = send_validate_message(ctxt, header, 0, NULL);

	CPRINTF("CTRL[%d]>%d\n", type, bit_len);

	return bit_len;
}

static void send_goodcrc(void *ctxt, int id)
{
	uint16_t header = PD_HEADER(PD_CTRL_GOOD_CRC, pd_role, id, 0);
	int bit_len = prepare_message(ctxt, header, 0, NULL);

	pd_start_tx(ctxt, pd_polarity, bit_len);
	pd_tx_done(pd_polarity);
}

static int send_source_cap(void *ctxt)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_SOURCE_CAP, pd_role, pd_message_id,
				    pd_src_pdo_cnt);

	bit_len = send_validate_message(ctxt, header, pd_src_pdo_cnt,
					pd_src_pdo);
	CPRINTF("srcCAP>%d\n", bit_len);

	return bit_len;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
static void send_sink_cap(void *ctxt)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_SINK_CAP, pd_role, pd_message_id,
				    pd_snk_pdo_cnt);

	bit_len = send_validate_message(ctxt, header, pd_snk_pdo_cnt,
					pd_snk_pdo);
	CPRINTF("snkCAP>%d\n", bit_len);
}

static int send_request(void *ctxt, uint32_t rdo)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_REQUEST, pd_role, pd_message_id, 1);

	bit_len = send_validate_message(ctxt, header, 1, &rdo);
	CPRINTF("REQ%d>\n", bit_len);

	return bit_len;
}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

static int send_bist_cmd(void *ctxt)
{
	/* currently only support sending bist carrier 2 */
	uint32_t bdo = BDO(BDO_MODE_CARRIER2, 0);
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_BIST, pd_role, pd_message_id, 1);

	bit_len = send_validate_message(ctxt, header, 1, &bdo);
	CPRINTF("BIST>%d\n", bit_len);

	return bit_len;
}

static void bist_mode_2_tx(void *ctxt)
{
	int bit;

	CPRINTF("BIST carrier 2 - sending\n");

	/*
	 * build context buffer with 5 bytes, where the data is
	 * alternating 1's and 0's.
	 */
	bit = pd_write_sym(ctxt, 0,   BMC(0x15));
	bit = pd_write_sym(ctxt, bit, BMC(0x0a));
	bit = pd_write_sym(ctxt, bit, BMC(0x15));
	bit = pd_write_sym(ctxt, bit, BMC(0x0a));

	/* start a circular DMA transfer (will never end) */
	pd_tx_set_circular_mode();
	pd_start_tx(ctxt, pd_polarity, bit);

	/* do not let pd task state machine run anymore */
	while (1)
		task_wait_event(-1);
}

static void bist_mode_2_rx(void)
{
	/* monitor for incoming packet */
	pd_rx_enable_monitoring();

	/* loop until we start receiving data */
	while (1) {
		task_wait_event(500*MSEC);
		/* incoming packet ? */
		if (pd_rx_started())
			break;
	}

	/*
	 * once we start receiving bist data, do not
	 * let state machine run again. stay here, and
	 * analyze a chunk of data every 250ms.
	 */
	while (1) {
		analyze_rx_bist();
		pd_rx_complete();
		msleep(250);
		pd_rx_enable_monitoring();
	}
}

static void handle_vdm_request(void *ctxt, int cnt, uint32_t *payload)
{
	uint16_t vid = PD_VDO_VID(payload[0]);
#ifdef CONFIG_USB_PD_CUSTOM_VDM
	int rlen;
	uint32_t *rdata;

	if (vid == USB_VID_GOOGLE) {
		rlen = pd_custom_vdm(ctxt, cnt, payload, &rdata);
		if (rlen > 0) {
			uint16_t header = PD_HEADER(PD_DATA_VENDOR_DEF,
						pd_role, pd_message_id, rlen);
			send_validate_message(ctxt, header, rlen, rdata);
		}
		return;
	}
#endif
	CPRINTF("Unhandled VDM VID %04x CMD %04x\n",
		vid, payload[0] & 0xFFFF);
}

static void handle_data_request(void *ctxt, uint16_t head, uint32_t *payload)
{
	int type = PD_HEADER_TYPE(head);
	int cnt = PD_HEADER_CNT(head);

	switch (type) {
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case PD_DATA_SOURCE_CAP:
		if ((pd_task_state == PD_STATE_SNK_DISCOVERY)
			|| (pd_task_state == PD_STATE_SNK_TRANSITION)) {
			uint32_t rdo;
			int res;
			/* we were waiting for them, let's process them */
			res = pd_choose_voltage(cnt, payload, &rdo);
			if (res >= 0) {
				res = send_request(ctxt, rdo);
				if (res >= 0)
					pd_task_state = PD_STATE_SNK_REQUESTED;
				else
					/*
					 * for now: ignore failure here,
					 * we will retry ...
					 * TODO(crosbug.com/p/28332)
					 */
					pd_task_state = PD_STATE_SNK_REQUESTED;
			}
			/*
			 * TODO(crosbug.com/p/28332): if pd_choose_voltage
			 * returns an error, ignore failure for now.
			 */
		}
		break;
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	case PD_DATA_REQUEST:
		if ((pd_role == PD_ROLE_SOURCE) && (cnt == 1))
			if (!pd_request_voltage(payload[0])) {
				send_control(ctxt, PD_CTRL_ACCEPT);
				pd_task_state = PD_STATE_SRC_ACCEPTED;
				return;
			}
		/* the message was incorrect or cannot be satisfied */
		send_control(ctxt, PD_CTRL_REJECT);
		break;
	case PD_DATA_BIST:
		/* currently only support sending bist carrier mode 2 */
		if ((payload[0] >> 28) == 5)
			/* bist data object mode is 2 */
			bist_mode_2_tx(ctxt);

		break;
	case PD_DATA_SINK_CAP:
		break;
	case PD_DATA_VENDOR_DEF:
		handle_vdm_request(ctxt, cnt, payload);
		break;
	default:
		CPRINTF("Unhandled data message type %d\n", type);
	}
}

static void handle_ctrl_request(void *ctxt, uint16_t head, uint32_t *payload)
{
	int type = PD_HEADER_TYPE(head);

	switch (type) {
	case PD_CTRL_GOOD_CRC:
		/* should not get it */
		break;
	case PD_CTRL_PING:
		/* Nothing else to do */
		break;
	case PD_CTRL_GET_SOURCE_CAP:
		send_source_cap(ctxt);
		break;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case PD_CTRL_GET_SINK_CAP:
		send_sink_cap(ctxt);
		break;
	case PD_CTRL_GOTO_MIN:
		break;
	case PD_CTRL_PS_RDY:
		if (pd_role == PD_ROLE_SINK)
			pd_task_state = PD_STATE_SNK_READY;
		break;
	case PD_CTRL_REJECT:
		pd_task_state = PD_STATE_SNK_DISCOVERY;
		break;
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	case PD_CTRL_ACCEPT:
		break;
	case PD_CTRL_SOFT_RESET:
		/* Just reset message counters */
		pd_message_id = 0;
		CPRINTF("Soft Reset\n");
		/* We are done, acknowledge with an Accept packet */
		send_control(ctxt, PD_CTRL_ACCEPT);
		break;
	case PD_CTRL_PROTOCOL_ERR:
	case PD_CTRL_SWAP:
	case PD_CTRL_WAIT:
	default:
		CPRINTF("Unhandled ctrl message type %d\n", type);
	}
}

static void handle_request(void *ctxt, uint16_t head, uint32_t *payload)
{
	int cnt = PD_HEADER_CNT(head);
	int p;

	if (PD_HEADER_TYPE(head) != 1 || cnt)
		send_goodcrc(ctxt, PD_HEADER_ID(head));

	/* dump received packet content */
	CPRINTF("RECV %04x/%d ", head, cnt);
	for (p = 0; p < cnt; p++)
		CPRINTF("[%d]%08x ", p, payload[p]);
	CPRINTF("\n");

	if (cnt)
		handle_data_request(ctxt, head, payload);
	else
		handle_ctrl_request(ctxt, head, payload);
}

static inline int decode_short(void *ctxt, int off, uint16_t *val16)
{
	uint32_t w;
	int end;

	end = pd_dequeue_bits(ctxt, off, 20, &w);

#if 0 /* DEBUG */
	CPRINTS("%d-%d: %05x %x:%x:%x:%x\n",
		off, end, w,
		dec4b5b[(w >> 15) & 0x1f], dec4b5b[(w >> 10) & 0x1f],
		dec4b5b[(w >>  5) & 0x1f], dec4b5b[(w >>  0) & 0x1f]);
#endif
	*val16 = dec4b5b[w & 0x1f] |
		(dec4b5b[(w >>  5) & 0x1f] << 4) |
		(dec4b5b[(w >> 10) & 0x1f] << 8) |
		(dec4b5b[(w >> 15) & 0x1f] << 12);
	return end;
}

static inline int decode_word(void *ctxt, int off, uint32_t *val32)
{
	off = decode_short(ctxt, off, (uint16_t *)val32);
	return decode_short(ctxt, off, ((uint16_t *)val32 + 1));
}

static int count_set_bits(int n)
{
	int count = 0;
	while (n) {
		n &= (n - 1);
		count++;
	}
	return count;
}

static void analyze_rx_bist(void)
{
	void *ctxt;
	int i = 0, bit = -1;
	uint32_t w, match;
	int invalid_bits = 0;
	static int total_invalid_bits;

	ctxt = pd_init_dequeue();

	/* dequeue bits until we see a full byte of alternating 1's and 0's */
	while (i < 10 && (bit < 0 || (w != 0xaa && w != 0x55)))
		bit = pd_dequeue_bits(ctxt, i++, 8, &w);

	/* if we didn't find any bytes that match criteria, display error */
	if (i == 10) {
		CPRINTF("Could not find any bytes of alternating bits\n");
		return;
	}

	/*
	 * now we know what matching byte we are looking for, dequeue a bunch
	 * more data and count how many bits differ from expectations.
	 */
	match = w;
	bit = i - 1;
	for (i = 0; i < 40; i++) {
		bit = pd_dequeue_bits(ctxt, bit, 8, &w);
		if (i % 20 == 0)
			CPRINTF("\n");
		CPRINTF("%02x ", w);
		invalid_bits += count_set_bits(w ^ match);
	}

	total_invalid_bits += invalid_bits;
	CPRINTF("- incorrect bits: %d / %d\n", invalid_bits,
			total_invalid_bits);
}

static int analyze_rx(uint32_t *payload)
{
	int bit;
	char *msg = "---";
	uint32_t val = 0;
	uint16_t header;
	uint32_t pcrc, ccrc;
	int p, cnt;
	/* uint32_t eop; */
	void *ctxt;

	crc32_init();
	ctxt = pd_init_dequeue();

	/* Detect preamble */
	bit = pd_find_preamble(ctxt);
	if (bit < 0) {
		msg = "Preamble";
		goto packet_err;
	}

	/* Find the Start Of Packet sequence */
	while (bit > 0) {
		bit = pd_dequeue_bits(ctxt, bit, 20, &val);
		if (val == PD_SOP)
			break;
		/* TODO: detect SOP with 1 error code */
		/* TODO: detect Hard reset */
	}
	if (bit < 0) {
		msg = "SOP";
		goto packet_err;
	}

	/* read header */
	bit = decode_short(ctxt, bit, &header);
	crc32_hash16(header);
	cnt = PD_HEADER_CNT(header);

	/* read payload data */
	for (p = 0; p < cnt && bit > 0; p++) {
		bit = decode_word(ctxt, bit, payload+p);
		crc32_hash32(payload[p]);
	}
	if (bit < 0) {
		msg = "len";
		goto packet_err;
	}

	/* check transmitted CRC */
	bit = decode_word(ctxt, bit, &pcrc);
	ccrc = crc32_result();
	if (bit < 0 || pcrc != ccrc) {
		msg = "CRC";
		if (pcrc != ccrc)
			bit = PD_ERR_CRC;
		/* DEBUG */CPRINTF("CRC %08x <> %08x\n", pcrc, crc32_result());
		goto packet_err;
	}

	/* check End Of Packet */
	/* SKIP EOP for now
	bit = pd_dequeue_bits(ctxt, bit, 5, &eop);
	if (bit < 0 || eop != PD_EOP) {
		msg = "EOP";
		goto packet_err;
	}
	*/

	return header;
packet_err:
	if (debug_dump)
		pd_dump_packet(ctxt, msg);
	else
		CPRINTF("RX ERR (%d)\n", bit);
	return bit;
}

static void execute_hard_reset(void)
{
	pd_message_id = 0;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	pd_task_state = pd_role == PD_ROLE_SINK ? PD_STATE_SNK_DISCONNECTED
						: PD_STATE_SRC_DISCONNECTED;
#else
	pd_task_state = PD_STATE_SRC_DISCONNECTED;
#endif
	pd_power_supply_reset();
	CPRINTF("HARD RESET!\n");
}

#ifdef BOARD_SAMUS_PD
extern void pd_charger_change(int c);
#endif
void pd_task(void)
{
	int head;
	void *ctxt = pd_hw_init();
	uint32_t payload[7];
	int timeout = 10*MSEC;
	int cc1_volt, cc2_volt;
	int res;

	/* Ensure the power supply is in the default state */
	pd_power_supply_reset();

	while (1) {
		/* monitor for incoming packet */
		pd_rx_enable_monitoring();
		/* Verify board specific health status : current, voltages... */
		res = pd_board_checks();
		if (res != EC_SUCCESS) {
			/* cut the power */
			execute_hard_reset();
			/* notify the other side of the issue */
			/* send_hard_reset(ctxt); */
		}
		/* wait for next event/packet or timeout expiration */
		task_wait_event(timeout);
		/* incoming packet ? */
		if (pd_rx_started()) {
			head = analyze_rx(payload);
			pd_rx_complete();
			if (head > 0)
				handle_request(ctxt, head, payload);
			else if (head == PD_ERR_HARD_RESET)
				execute_hard_reset();
		}
		/* if nothing to do, verify the state of the world in 500ms */
		timeout = 500*MSEC;
		switch (pd_task_state) {
		case PD_STATE_DISABLED:
			/* Nothing to do */
			break;
		case PD_STATE_SRC_DISCONNECTED:
			/* Vnc monitoring */
			cc1_volt = pd_adc_read(0);
			cc2_volt = pd_adc_read(1);
			if ((cc1_volt < PD_SRC_VNC) ||
			    (cc2_volt < PD_SRC_VNC)) {
				pd_polarity = !(cc1_volt < PD_SRC_VNC);
				pd_select_polarity(pd_polarity);
				/* Enable VBUS */
				pd_set_power_supply_ready();
				pd_task_state = PD_STATE_SRC_DISCOVERY;
			}
			timeout = 10*MSEC;
			break;
		case PD_STATE_SRC_DISCOVERY:
			/* Detect disconnect by monitoring Vnc */
			cc1_volt = pd_adc_read(pd_polarity);
			if (cc1_volt > PD_SRC_VNC) {
				/* The sink disappeared ... */
				pd_power_supply_reset();
				pd_task_state = PD_STATE_SRC_DISCONNECTED;
				/* Debouncing */
				timeout = 50*MSEC;
				break;
			}
			/* Query capabilites of the other side */
			res = send_source_cap(ctxt);
			/* packet was acked => PD capable device) */
			if (res >= 0) {
				pd_task_state = PD_STATE_SRC_NEGOCIATE;
			} else { /* failed, retry later */
				timeout = PD_T_SEND_SOURCE_CAP;
			}
			break;
		case PD_STATE_SRC_NEGOCIATE:
			/* wait for a "Request" message */
			break;
		case PD_STATE_SRC_ACCEPTED:
			/* Accept sent, wait for the end of transition */
			timeout = PD_POWER_SUPPLY_TRANSITION_DELAY;
			pd_task_state = PD_STATE_SRC_TRANSITION;
			break;
		case PD_STATE_SRC_TRANSITION:
			res = pd_set_power_supply_ready();
			/* TODO error fallback */
			/* the voltage output is good, notify the source */
			res = send_control(ctxt, PD_CTRL_PS_RDY);
			if (res >= 0) {
				timeout =  PD_T_SEND_SOURCE_CAP;
				/* it'a time to ping regularly the sink */
				pd_task_state = PD_STATE_SRC_READY;
			}
			/* TODO error fallback */
			break;
		case PD_STATE_SRC_READY:
			/* Verify that the sink is alive */
			res = send_control(ctxt, PD_CTRL_PING);
			if (res < 0) {
				/* The sink died ... */
				pd_power_supply_reset();
				pd_task_state = PD_STATE_SRC_DISCOVERY;
				timeout = PD_T_SEND_SOURCE_CAP;
			} else { /* schedule next keep-alive */
				timeout = PD_T_SOURCE_ACTIVITY;
			}
			break;
#ifdef CONFIG_USB_PD_DUAL_ROLE
		case PD_STATE_SNK_DISCONNECTED:
			/* Source connection monitoring */
#ifdef BOARD_SAMUS_PD
			/*
			 * TODO(crosbug.com/p/29841): remove hack for
			 * getting extpower is present status from PD MCU.
			 */
			pd_charger_change(0);
#endif
			cc1_volt = pd_adc_read(0);
			cc2_volt = pd_adc_read(1);
			if ((cc1_volt > PD_SNK_VA) ||
			    (cc2_volt > PD_SNK_VA)) {
				pd_polarity = !(cc1_volt > PD_SNK_VA);
				pd_select_polarity(pd_polarity);
				pd_task_state = PD_STATE_SNK_DISCOVERY;
			}
			timeout = 10*MSEC;
			break;
		case PD_STATE_SNK_DISCOVERY:
			/* For non-PD aware source, detect source disconnect */
			cc1_volt = pd_adc_read(pd_polarity);
			if (cc1_volt < PD_SNK_VA) {
				/* The source disappeared ... */
				pd_task_state = PD_STATE_SNK_DISCONNECTED;
				/* Debouncing */
				timeout = 50*MSEC;
				break;
			}

			/* Don't continue if power negotiation is not allowed */
			if (!pd_power_negotiation_allowed()) {
				timeout = PD_T_GET_SOURCE_CAP;
				break;
			}

			res = send_control(ctxt, PD_CTRL_GET_SOURCE_CAP);
			/* packet was acked => PD capable device) */
			if (res >= 0) {
				/*
				 * we should a SOURCE_CAP package which will
				 * switch to the PD_STATE_SNK_REQUESTED state,
				 * else retry after the response timeout.
				 */
				timeout = PD_T_SENDER_RESPONSE;
			} else { /* failed, retry later */
				timeout = PD_T_GET_SOURCE_CAP;
			}
			break;
		case PD_STATE_SNK_REQUESTED:
			/* Ensure the power supply actually becomes ready */
			pd_task_state = PD_STATE_SNK_TRANSITION;
			timeout = PD_T_PS_TRANSITION;
			break;
		case PD_STATE_SNK_TRANSITION:
			/*
			 * did not get the PS_READY,
			 * try again to whole request cycle.
			 */
			pd_task_state = PD_STATE_SNK_DISCOVERY;
			timeout = 10*MSEC;
			break;
		case PD_STATE_SNK_READY:
			/* we have power and we are happy */
#ifdef BOARD_SAMUS_PD
			/*
			 * TODO(crosbug.com/p/29841): remove hack for
			 * getting extpower is present status from PD MCU.
			 */
			pd_charger_change(1);
#endif

			/* if we have lost vbus, go back to disconnected */
			if (!pd_snk_is_vbus_provided()) {
				pd_task_state = PD_STATE_SNK_DISCONNECTED;
				/* set timeout small to reconnect fast */
				timeout = 5*MSEC;
				break;
			}

			/* check vital parameters from time to time */
			timeout = 100*MSEC;
			break;
#endif /* CONFIG_USB_PD_DUAL_ROLE */
		case PD_STATE_HARD_RESET:
			send_hard_reset(ctxt);
			/* reset our own state machine */
			execute_hard_reset();
			break;
		case PD_STATE_BIST:
			send_bist_cmd(ctxt);
			bist_mode_2_rx();
			break;
		}
	}
}

void pd_rx_event(void)
{
	task_set_event(TASK_ID_PD, PD_EVENT_RX, 0);
}

#ifdef CONFIG_COMMON_RUNTIME
void pd_request_source_voltage(int mv)
{
	pd_set_max_voltage(mv);
	pd_role = PD_ROLE_SINK;
	pd_set_host_mode(0);
	pd_task_state = PD_STATE_SNK_DISCONNECTED;
	task_wake(TASK_ID_PD);
}

static int command_pd(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[1], "tx")) {
		pd_task_state = PD_STATE_SNK_DISCOVERY;
		task_wake(TASK_ID_PD);
	} else if (!strcasecmp(argv[1], "bist")) {
		pd_task_state = PD_STATE_BIST;
		task_wake(TASK_ID_PD);
	} else if (!strcasecmp(argv[1], "charger")) {
		pd_role = PD_ROLE_SOURCE;
		pd_set_host_mode(1);
		pd_task_state = PD_STATE_SRC_DISCONNECTED;
		task_wake(TASK_ID_PD);
	} else if (!strncasecmp(argv[1], "dev", 3)) {
		int max_volt = -1;
		if (argc >= 3) {
			char *e;
			max_volt = strtoi(argv[2], &e, 10) * 1000;
		}
		pd_request_source_voltage(max_volt);
	} else if (!strcasecmp(argv[1], "clock")) {
		int freq;
		char *e;

		if (argc < 3)
			return EC_ERROR_PARAM2;

		freq = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;
		pd_set_clock(freq);
		ccprintf("set TX frequency to %d Hz\n", freq);
	} else if (!strcasecmp(argv[1], "dump")) {
		debug_dump = !debug_dump;
	} else if (!strncasecmp(argv[1], "hard", 4)) {
		pd_task_state = PD_STATE_HARD_RESET;
		task_wake(TASK_ID_PD);
	} else if (!strncasecmp(argv[1], "ping", 4)) {
		pd_role = PD_ROLE_SOURCE;
		pd_set_host_mode(1);
		pd_task_state = PD_STATE_SRC_READY;
		task_wake(TASK_ID_PD);
	} else if (!strncasecmp(argv[1], "state", 5)) {
		const char * const state_names[] = {
			"DISABLED",
			"SNK_DISCONNECTED", "SNK_DISCOVERY", "SNK_REQUESTED",
			"SNK_TRANSITION", "SNK_READY",
			"SRC_DISCONNECTED", "SRC_DISCOVERY", "SRC_NEGOCIATE",
			"SRC_ACCEPTED", "SRC_TRANSITION", "SRC_READY",
			"HARD_RESET", "BIST",
		};
		ccprintf("Role: %s Polarity: CC%d State: %s\n",
			pd_role == PD_ROLE_SOURCE ? "SRC" : "SNK",
			pd_polarity + 1, state_names[pd_task_state]);
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pd, command_pd,
			"[rx|tx|hardreset|clock|connect]",
			"USB PD",
			NULL);
#endif /* CONFIG_COMMON_RUNTIME */
