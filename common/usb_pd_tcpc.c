/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "crc.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_pd_tcpm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/*
 * Debug log level - higher number == more log
 *   Level 0: Log state transitions
 *   Level 1: Level 0, plus packet info
 *   Level 2: Level 1, plus ping packet and packet dump on error
 *
 * Note that higher log level causes timing changes and thus may affect
 * performance.
 */
static int debug_level;

/*
 * TODO: disable in RO? can we remove enable var from protocol layer?
 * do we need to send a hard reset when we transition to enabled because
 * source could have given up sending source cap and may need hard reset
 * in order to establish a contract.
 */
static uint8_t pd_comm_enabled = 1;

static struct mutex pd_crc_lock;
#else
#define CPRINTF(format, args...)
static const int debug_level;
static const int pd_comm_enabled = 1;
#endif

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
#define PD_SOP_PRIME	(PD_SYNC1 | (PD_SYNC1<<5) | \
			(PD_SYNC3<<10) | (PD_SYNC3<<15))
#define PD_SOP_PRIME_PRIME	(PD_SYNC1 | (PD_SYNC3<<5) | \
				(PD_SYNC1<<10) | (PD_SYNC3<<15))

/* Hard Reset sequence : three RST-1 K-codes, then one RST-2 K-code */
#define PD_HARD_RESET (PD_RST1 | (PD_RST1 << 5) |\
		      (PD_RST1 << 10) | (PD_RST2 << 15))

/*
 * Polarity based on 'DFP Perspective' (see table USB Type-C Cable and Connector
 * Specification)
 *
 * CC1    CC2    STATE             POSITION
 * ----------------------------------------
 * open   open   NC                N/A
 * Rd     open   UFP attached      1
 * open   Rd     UFP attached      2
 * open   Ra     pwr cable no UFP  N/A
 * Ra     open   pwr cable no UFP  N/A
 * Rd     Ra     pwr cable & UFP   1
 * Ra     Rd     pwr cable & UFP   2
 * Rd     Rd     dbg accessory     N/A
 * Ra     Ra     audio accessory   N/A
 *
 * Note, V(Rd) > V(Ra)
 */
#ifndef PD_SRC_RD_THRESHOLD
#define PD_SRC_RD_THRESHOLD  200 /* mV */
#endif
#define CC_RA(cc)  (cc < PD_SRC_RD_THRESHOLD)
#define CC_RD(cc) ((cc >= PD_SRC_RD_THRESHOLD) && (cc < PD_SRC_VNC))
#define CC_NC(cc)  (cc >= PD_SRC_VNC)

/*
 * Polarity based on 'UFP Perspective'.
 *
 * CC1    CC2    STATE             POSITION
 * ----------------------------------------
 * open   open   NC                N/A
 * Rp     open   DFP attached      1
 * open   Rp     DFP attached      2
 * Rp     Rp     Accessory attached N/A
 */
#define CC_RP(cc)  (cc >= PD_SNK_VA)

/*
 * Type C power source charge current limits are identified by their cc
 * voltage (set by selecting the proper Rd resistor). Any voltage below
 * TYPE_C_SRC_500_THRESHOLD will not be identified as a type C charger.
 */
#define TYPE_C_SRC_500_THRESHOLD	PD_SRC_RD_THRESHOLD
#define TYPE_C_SRC_1500_THRESHOLD	660  /* mV */
#define TYPE_C_SRC_3000_THRESHOLD	1230 /* mV */

/* PD transmit errors */
enum pd_tx_errors {
	PD_TX_ERR_GOODCRC = -1, /* Failed to receive goodCRC */
	PD_TX_ERR_DISABLED = -2, /* Attempted transmit even though disabled */
	PD_TX_ERR_INV_ACK = -4, /* Received different packet instead of gCRC */
	PD_TX_ERR_COLLISION = -5 /* Collision detected during transmit */
};

static struct pd_port_controller {
	/* current port power role (SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
	uint8_t polarity;
	/* Our CC pull resistor setting */
	uint8_t cc_pull;
	/* CC status */
	uint8_t cc_status[2];
	/* TCPC alert status */
	uint8_t alert[2];

	/* Last received */
	int rx_head;
	uint32_t rx_payload[7];

	/* Next transmit */
	enum tcpm_transmit_type tx_type;
	uint16_t tx_head;
	const uint32_t *tx_data;
} pd[PD_PORT_COUNT];

static inline int encode_short(int port, int off, uint16_t val16)
{
	off = pd_write_sym(port, off, bmc4b5b[(val16 >> 0) & 0xF]);
	off = pd_write_sym(port, off, bmc4b5b[(val16 >> 4) & 0xF]);
	off = pd_write_sym(port, off, bmc4b5b[(val16 >> 8) & 0xF]);
	return pd_write_sym(port, off, bmc4b5b[(val16 >> 12) & 0xF]);
}

int encode_word(int port, int off, uint32_t val32)
{
	off = encode_short(port, off, (val32 >> 0) & 0xFFFF);
	return encode_short(port, off, (val32 >> 16) & 0xFFFF);
}

/* prepare a 4b/5b-encoded PD message to send */
int prepare_message(int port, uint16_t header, uint8_t cnt,
		   const uint32_t *data)
{
	int off, i;
	/* 64-bit preamble */
	off = pd_write_preamble(port);
	/* Start Of Packet: 3x Sync-1 + 1x Sync-2 */
	off = pd_write_sym(port, off, BMC(PD_SYNC1));
	off = pd_write_sym(port, off, BMC(PD_SYNC1));
	off = pd_write_sym(port, off, BMC(PD_SYNC1));
	off = pd_write_sym(port, off, BMC(PD_SYNC2));
	/* header */
	off = encode_short(port, off, header);

#ifdef CONFIG_COMMON_RUNTIME
	mutex_lock(&pd_crc_lock);
#endif

	crc32_init();
	crc32_hash16(header);
	/* data payload */
	for (i = 0; i < cnt; i++) {
		off = encode_word(port, off, data[i]);
		crc32_hash32(data[i]);
	}
	/* CRC */
	off = encode_word(port, off, crc32_result());

#ifdef CONFIG_COMMON_RUNTIME
	mutex_unlock(&pd_crc_lock);
#endif

	/* End Of Packet */
	off = pd_write_sym(port, off, BMC(PD_EOP));
	/* Ensure that we have a final edge */
	return pd_write_last_edge(port, off);
}

static int send_hard_reset(int port)
{
	int off;

	if (debug_level >= 1)
		CPRINTF("C%d Send hard reset\n", port);

	/* 64-bit preamble */
	off = pd_write_preamble(port);
	/* Hard-Reset: 3x RST-1 + 1x RST-2 */
	off = pd_write_sym(port, off, BMC(PD_RST1));
	off = pd_write_sym(port, off, BMC(PD_RST1));
	off = pd_write_sym(port, off, BMC(PD_RST1));
	off = pd_write_sym(port, off, BMC(PD_RST2));
	/* Ensure that we have a final edge */
	off = pd_write_last_edge(port, off);
	/* Transmit the packet */
	if (pd_start_tx(port, pd[port].polarity, off) < 0)
		return PD_TX_ERR_COLLISION;
	pd_tx_done(port, pd[port].polarity);
	/* Keep RX monitoring on */
	pd_rx_enable_monitoring(port);
	return 0;
}

static int send_validate_message(int port, uint16_t header,
				 const uint32_t *data)
{
	int r;
	static uint32_t payload[7];
	uint8_t expected_msg_id = PD_HEADER_ID(header);
	uint8_t cnt = PD_HEADER_CNT(header);

	/* retry 3 times if we are not getting a valid answer */
	for (r = 0; r <= PD_RETRY_COUNT; r++) {
		int bit_len, head;
		/* write the encoded packet in the transmission buffer */
		bit_len = prepare_message(port, header, cnt, data);
		/* Transmit the packet */
		if (pd_start_tx(port, pd[port].polarity, bit_len) < 0) {
			/*
			 * Collision detected, return immediately so we can
			 * respond to what we have received.
			 */
			return PD_TX_ERR_COLLISION;
		}
		pd_tx_done(port, pd[port].polarity);
		/*
		 * If this is the first attempt, leave RX monitoring off,
		 * and do a blocking read of the channel until timeout or
		 * packet received. If we failed the first try, enable
		 * interrupt and yield to other tasks, so that we don't
		 * starve them.
		 */
		if (r) {
			pd_rx_enable_monitoring(port);
			/* Wait for message receive timeout */
			if (task_wait_event(USB_PD_RX_TMOUT_US) ==
			    TASK_EVENT_TIMER)
				continue;
			/*
			 * Make sure we woke up due to rx recd, otherwise
			 * we need to manually start
			 */
			if (!pd_rx_started(port)) {
				pd_rx_disable_monitoring(port);
				pd_rx_start(port);
			}
		} else {
			/* starting waiting for GoodCrc */
			pd_rx_start(port);
		}
		/* read the incoming packet if any */
		head = pd_analyze_rx(port, payload);
		pd_rx_complete(port);
		/* keep RX monitoring on to avoid collisions */
		pd_rx_enable_monitoring(port);
		if (head > 0) { /* we got a good packet, analyze it */
			int type = PD_HEADER_TYPE(head);
			int nb = PD_HEADER_CNT(head);
			uint8_t id = PD_HEADER_ID(head);
			if (type == PD_CTRL_GOOD_CRC && nb == 0 &&
			    id == expected_msg_id) {
				/* got the GoodCRC we were expecting */
				/* do not catch last edges as a new packet */
				udelay(20);
				return bit_len;
			} else {
				/*
				 * we have received a good packet
				 * but not the expected GoodCRC,
				 * the other side is trying to contact us,
				 * bail out immediatly so we can get the retry.
				 */
				return PD_TX_ERR_INV_ACK;
			}
		}
	}
	/* we failed all the re-transmissions */
	if (debug_level >= 1)
		CPRINTF("TX NOACK%d %04x/%d\n", port, header, cnt);
	return PD_TX_ERR_GOODCRC;
}

static void send_goodcrc(int port, int id)
{
	uint16_t header = PD_HEADER(PD_CTRL_GOOD_CRC, pd[port].power_role,
			pd[port].data_role, id, 0);
	int bit_len = prepare_message(port, header, 0, NULL);

	if (pd_start_tx(port, pd[port].polarity, bit_len) < 0)
		/* another packet recvd before we could send goodCRC */
		return;
	pd_tx_done(port, pd[port].polarity);
	/* Keep RX monitoring on */
	pd_rx_enable_monitoring(port);
}

#if 0
/* TODO: when/how do we trigger this ? */
static int analyze_rx_bist(int port);

void bist_mode_2_rx(int port)
{
	int analyze_bist = 0;
	int num_bits;
	timestamp_t start_time;

	/* monitor for incoming packet */
	pd_rx_enable_monitoring(port);

	/* loop until we start receiving data */
	start_time.val = get_time().val;
	while ((get_time().val - start_time.val) < (500*MSEC)) {
		task_wait_event(10*MSEC);
		/* incoming packet ? */
		if (pd_rx_started(port)) {
			analyze_bist = 1;
			break;
		}
	}

	if (analyze_bist) {
		/*
		 * once we start receiving bist data, analyze 40 bytes
		 * every 10 msec. Continue analyzing until BIST data
		 * is no longer received. The standard limits the max
		 * BIST length to 60 msec.
		 */
		start_time.val = get_time().val;
		while ((get_time().val - start_time.val)
			< (PD_T_BIST_RECEIVE)) {
			num_bits = analyze_rx_bist(port);
			pd_rx_complete(port);
			/*
			 * If no data was received, then analyze_rx_bist()
			 * will return a -1 and there is no need to stay
			 * in this mode
			 */
			if (num_bits == -1)
				break;
			msleep(10);
			pd_rx_enable_monitoring(port);
		}
	} else {
		CPRINTF("BIST RX TO\n");
	}
}
#endif

static void bist_mode_2_tx(int port)
{
	int bit;

	CPRINTF("BIST 2: p%d\n", port);
	/*
	 * build context buffer with 5 bytes, where the data is
	 * alternating 1's and 0's.
	 */
	bit = pd_write_sym(port, 0,   BMC(0x15));
	bit = pd_write_sym(port, bit, BMC(0x0a));
	bit = pd_write_sym(port, bit, BMC(0x15));
	bit = pd_write_sym(port, bit, BMC(0x0a));

	/* start a circular DMA transfer */
	pd_tx_set_circular_mode(port);
	pd_start_tx(port, pd[port].polarity, bit);

	task_wait_event(PD_T_BIST_TRANSMIT);

	/* clear dma circular mode, will also stop dma */
	pd_tx_clear_circular_mode(port);
	/* finish and cleanup transmit */
	pd_tx_done(port, pd[port].polarity);
}

static inline int decode_short(int port, int off, uint16_t *val16)
{
	uint32_t w;
	int end;

	end = pd_dequeue_bits(port, off, 20, &w);

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

static inline int decode_word(int port, int off, uint32_t *val32)
{
	off = decode_short(port, off, (uint16_t *)val32);
	return decode_short(port, off, ((uint16_t *)val32 + 1));
}

#ifdef CONFIG_COMMON_RUNTIME
#if 0
/*
 * TODO: when/how do we trigger this ? Could add custom vendor command
 * to TCPCI to enter bist verification? Is there an easier way?
 */
static int count_set_bits(int n)
{
	int count = 0;
	while (n) {
		n &= (n - 1);
		count++;
	}
	return count;
}

static int analyze_rx_bist(int port)
{
	int i = 0, bit = -1;
	uint32_t w, match;
	int invalid_bits = 0;
	int bits_analyzed = 0;
	static int total_invalid_bits;

	/* dequeue bits until we see a full byte of alternating 1's and 0's */
	while (i < 10 && (bit < 0 || (w != 0xaa && w != 0x55)))
		bit = pd_dequeue_bits(port, i++, 8, &w);

	/* if we didn't find any bytes that match criteria, display error */
	if (i == 10) {
		CPRINTF("invalid pattern\n");
		return -1;
	}
	/*
	 * now we know what matching byte we are looking for, dequeue a bunch
	 * more data and count how many bits differ from expectations.
	 */
	match = w;
	bit = i - 1;
	for (i = 0; i < 40; i++) {
		bit = pd_dequeue_bits(port, bit, 8, &w);
		if (i && (i % 20 == 0))
			CPRINTF("\n");
		CPRINTF("%02x ", w);
		bits_analyzed += 8;
		invalid_bits += count_set_bits(w ^ match);
	}

	total_invalid_bits += invalid_bits;

	CPRINTF("\nInvalid: %d/%d\n",
		invalid_bits, total_invalid_bits);
	return bits_analyzed;
}
#endif
#endif

int pd_analyze_rx(int port, uint32_t *payload)
{
	int bit;
	char *msg = "---";
	uint32_t val = 0;
	uint16_t header;
	uint32_t pcrc, ccrc;
	int p, cnt;
	uint32_t eop;

	pd_init_dequeue(port);

	/* Detect preamble */
	bit = pd_find_preamble(port);
	if (bit == PD_RX_ERR_HARD_RESET || bit == PD_RX_ERR_CABLE_RESET) {
		/* Hard reset or cable reset */
		return bit;
	} else if (bit < 0) {
		msg = "Preamble";
		goto packet_err;
	}

	/* Find the Start Of Packet sequence */
	while (bit > 0) {
		bit = pd_dequeue_bits(port, bit, 20, &val);
		if (val == PD_SOP) {
			break;
		} else if (val == PD_SOP_PRIME) {
			CPRINTF("SOP'\n");
			return PD_RX_ERR_UNSUPPORTED_SOP;
		} else if (val == PD_SOP_PRIME_PRIME) {
			CPRINTF("SOP''\n");
			return PD_RX_ERR_UNSUPPORTED_SOP;
		}
	}
	if (bit < 0) {
		msg = "SOP";
		goto packet_err;
	}

	/* read header */
	bit = decode_short(port, bit, &header);

#ifdef CONFIG_COMMON_RUNTIME
	mutex_lock(&pd_crc_lock);
#endif

	crc32_init();
	crc32_hash16(header);
	cnt = PD_HEADER_CNT(header);

	/* read payload data */
	for (p = 0; p < cnt && bit > 0; p++) {
		bit = decode_word(port, bit, payload+p);
		crc32_hash32(payload[p]);
	}
	ccrc = crc32_result();

#ifdef CONFIG_COMMON_RUNTIME
	mutex_unlock(&pd_crc_lock);
#endif

	if (bit < 0) {
		msg = "len";
		goto packet_err;
	}

	/* check transmitted CRC */
	bit = decode_word(port, bit, &pcrc);
	if (bit < 0 || pcrc != ccrc) {
		msg = "CRC";
		if (pcrc != ccrc)
			bit = PD_RX_ERR_CRC;
		if (debug_level >= 1)
			CPRINTF("CRC%d %08x <> %08x\n", port, pcrc, ccrc);
		goto packet_err;
	}

	/*
	 * Check EOP. EOP is 5 bits, but last bit may not be able to
	 * be dequeued, depending on ending state of CC line, so stop
	 * at 4 bits (assumes last bit is 0).
	 */
	bit = pd_dequeue_bits(port, bit, 4, &eop);
	if (bit < 0 || eop != PD_EOP) {
		msg = "EOP";
		goto packet_err;
	}

	return header;
packet_err:
	if (debug_level >= 2)
		pd_dump_packet(port, msg);
	else
		CPRINTF("RXERR%d %s\n", port, msg);
	return bit;
}

static void handle_request(int port, uint16_t head,
		uint32_t *payload)
{
	int cnt = PD_HEADER_CNT(head);

	if (PD_HEADER_TYPE(head) != PD_CTRL_GOOD_CRC || cnt)
		send_goodcrc(port, PD_HEADER_ID(head));
	else
		/* keep RX monitoring on to avoid collisions */
		pd_rx_enable_monitoring(port);
}

/* Convert CC voltage to CC status */
static int cc_voltage_to_status(int port, int cc_volt)
{
	/* If we have a pull-up, then we are source, check for Rd. */
	if (pd[port].cc_pull == TYPEC_CC_RP) {
		if (CC_NC(cc_volt))
			return TYPEC_CC_SRC_OPEN;
		else if (CC_RA(cc_volt))
			return TYPEC_CC_SRC_RA;
		else
			return TYPEC_CC_SRC_RD;
	/* If we have a pull-down, then we are sink, check for Rp. */
	}
#ifdef CONFIG_USB_PD_DUAL_ROLE
	else if (pd[port].cc_pull == TYPEC_CC_RD) {
		if (cc_volt >= TYPE_C_SRC_3000_THRESHOLD)
			return TYPEC_CC_SNK_PWR_3_0;
		else if (cc_volt >= TYPE_C_SRC_1500_THRESHOLD)
			return TYPEC_CC_SNK_PWR_1_5;
		else if (CC_RP(cc_volt))
			return TYPEC_CC_SNK_PWR_DEFAULT;
		else
			return TYPEC_CC_SNK_OPEN;
	}
#endif
	/* If we are open, then always return 0 */
	else
		return 0;
}

static void alert(int port, int reg, int mask)
{
	pd[port].alert[reg] |= mask;
	tcpc_alert();
}

void tcpc_init(int port)
{
	/* Initialize physical layer */
	pd_hw_init(port, PD_ROLE_DEFAULT);

	/* make sure PD monitoring is enabled to wake on PD RX */
	if (pd_comm_enabled)
		pd_rx_enable_monitoring(port);

}

int tcpc_run(int port, int evt)
{
	int cc, i, res;

	/* incoming packet ? */
	if (pd_rx_started(port) && pd_comm_enabled) {
		pd[port].rx_head = pd_analyze_rx(port,
						 pd[port].rx_payload);
		pd_rx_complete(port);
		if (pd[port].rx_head > 0) {
			handle_request(port,
				       pd[port].rx_head,
				       pd[port].rx_payload);
			alert(port, TCPC_ALERT0, TCPC_ALERT0_RX_STATUS);
		} else if (pd[port].rx_head == PD_RX_ERR_HARD_RESET) {
			alert(port, TCPC_ALERT0,
			      TCPC_ALERT0_RX_HARD_RST);
		}
	}

	/* outgoing packet ? */
	if ((evt & PD_EVENT_TX) && pd_comm_enabled) {
		switch (pd[port].tx_type) {
		case TRANSMIT_SOP:
			res = send_validate_message(port,
					pd[port].tx_head,
					pd[port].tx_data);
			break;
		case TRANSMIT_BIST_MODE_2:
			bist_mode_2_tx(port);
			res = 0;
			break;
		case TRANSMIT_HARD_RESET:
			res = send_hard_reset(port);
			break;
		default:
			break;
		}

		/* send appropriate alert for tx completion */
		if (res >= 0)
			alert(port, TCPC_ALERT0,
			      TCPC_ALERT0_TX_SUCCESS);
		else if (res == PD_TX_ERR_GOODCRC)
			alert(port, TCPC_ALERT0,
			      TCPC_ALERT0_TX_FAILED);
		else
			alert(port, TCPC_ALERT0,
			      TCPC_ALERT0_TX_DISCARDED);
	}

	/* CC pull changed, wait 1ms for CC voltage to stabilize */
	if (evt & PD_EVENT_CC)
		usleep(MSEC);

	/* check CC lines */
	for (i = 0; i < 2; i++) {
		/* read CC voltage */
		cc = pd_adc_read(port, i);

		/* convert voltage to status, and check status change */
		cc = cc_voltage_to_status(port, cc);
		if (pd[port].cc_status[i] != cc) {
			pd[port].cc_status[i] = cc;
			alert(port, TCPC_ALERT0, TCPC_ALERT0_CC_STATUS);
		}
	}

	/* make sure PD monitoring is enabled to wake on PD RX */
	if (pd_comm_enabled)
		pd_rx_enable_monitoring(port);

	/* TODO: adjust timeout based on how often to sample CC */
	return 10*MSEC;
}

#ifndef CONFIG_USB_POWER_DELIVERY
void pd_task(void)
{
	int port = TASK_ID_TO_PORT(task_get_current());
	int timeout = 10*MSEC;
	int evt;

	/* initialize phy task */
	tcpc_init(port);

	while (1) {
		/* wait for next event/packet or timeout expiration */
		evt = task_wait_event(timeout);

		/* run phy task once */
		timeout = tcpc_run(port, evt);
	}
}
#endif

void pd_rx_event(int port)
{
	task_set_event(PORT_TO_TASK_ID(port), PD_EVENT_RX, 0);
}

int tcpc_alert_status(int port, int alert_reg)
{
	int ret = pd[port].alert[alert_reg];

	/* TODO: Alert register is read-clear for now, but shouldn't be */
	pd[port].alert[alert_reg] = 0;
	return ret;
}

void tcpc_set_cc(int port, int pull)
{
	/* If CC pull resistor not changing, then nothing to do */
	if (pd[port].cc_pull == pull)
		return;

	/* Change CC pull resistor */
	pd[port].cc_pull = pull;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	pd_set_host_mode(port, pull == TYPEC_CC_RP);
#endif

	/*
	 * Before CC pull can be changed and the task can read the new
	 * status, we should set the CC status to open, in case TCPM
	 * asks before it is known for sure.
	 */
	pd[port].cc_status[0] = pull == TYPEC_CC_RP ? TYPEC_CC_SRC_OPEN :
						      TYPEC_CC_SNK_OPEN;
	pd[port].cc_status[1] = pd[port].cc_status[0];

	/* Wake the PD phy task with special CC event mask */
	/* TODO: use top case if no TCPM on same CPU */
#ifdef CONFIG_USB_POWER_DELIVERY
	tcpc_run(port, PD_EVENT_CC);
#else
	task_set_event(PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
#endif
}

int tcpc_get_cc(int port, int polarity)
{
	return pd[port].cc_status[polarity];
}

void tcpc_set_polarity(int port, int polarity)
{
	pd[port].polarity = polarity;
	pd_select_polarity(port, pd[port].polarity);
}

void tcpc_set_vconn(int port, int enable)
{
#ifdef CONFIG_USBC_VCONN
	pd_set_vconn(port, pd[port].polarity, enable);
#endif
}

void tcpc_transmit(int port, enum tcpm_transmit_type type, uint16_t header,
		   const uint32_t *data)
{
	/* Store data to transmit and wake task to send it */
	pd[port].tx_type = type;
	pd[port].tx_head = header;
	pd[port].tx_data = data;
	/* TODO: use top case if no TCPM on same CPU */
#ifdef CONFIG_USB_POWER_DELIVERY
	tcpc_run(port, PD_EVENT_TX);
#else
	task_set_event(PORT_TO_TASK_ID(port), PD_EVENT_TX, 0);
#endif
}

void tcpc_set_msg_header(int port, int power_role, int data_role)
{
	pd[port].power_role = power_role;
	pd[port].data_role = data_role;
}

int tcpc_get_message(int port, uint32_t *payload)
{
	memcpy(payload, pd[port].rx_payload, sizeof(pd[port].rx_payload));
	return pd[port].rx_head;
}

#ifdef CONFIG_COMMON_RUNTIME
static int command_tcpc(int argc, char **argv)
{
	int port;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "dump")) {
		int level;

		if (argc < 3)
			ccprintf("lvl: %d\n", debug_level);
		else {
			level = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
			debug_level = level;
		}
		return EC_SUCCESS;
	} else if (!strcasecmp(argv[1], "enable")) {
		int enable;

		if (argc < 3)
			return EC_ERROR_PARAM_COUNT;

		enable = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM3;
		pd_comm_enabled = enable;
		ccprintf("Ports %s\n", enable ? "enabled" : "disabled");
		return EC_SUCCESS;
	}

	/* command: pd <port> <subcmd> [args] */
	port = strtoi(argv[1], &e, 10);
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (*e || port >= PD_PORT_COUNT)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[2], "clock")) {
		int freq;

		if (argc < 4)
			return EC_ERROR_PARAM2;

		freq = strtoi(argv[3], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;
		pd_set_clock(port, freq);
		ccprintf("set TX frequency to %d Hz\n", freq);
		return EC_SUCCESS;
	} else if (!strncasecmp(argv[2], "state", 5)) {
		ccprintf("Port C%d, %s - CC:%d, CC0:%d, CC1:%d, "
			 "Alert: 0x%02x 0x%02x\n", port,
			 pd_comm_enabled ? "Ena" : "Dis",
			 pd[port].cc_pull,
			 pd[port].cc_status[0], pd[port].cc_status[1],
			 pd[port].alert[0], pd[port].alert[1]);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tcpc, command_tcpc,
			"dump|enable [0|1]\n\t<port> [clock|state]",
			"Type-C Port Controller",
			NULL);
#endif
