/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "crc.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

#define PREAMBLE_OFFSET 60 /* Any number should do */

/*
 * Maximum size of a Power Delivery packet (in bits on the wire) :
 *    16-bit header + 0..7 32-bit data objects  (+ 4b5b encoding)
 * 64-bit preamble + SOP (4x 5b) + message in 4b5b + 32-bit CRC + EOP (1x 5b)
 * = 64 + 4*5 + 16 * 5/4 + 7 * 32 * 5/4 + 32 * 5/4 + 5
 */
#define PD_BIT_LEN 429

static struct pd_physical {
	int hw_init_done;

	uint8_t bits[PD_BIT_LEN];
	int total;
	int has_preamble;
	int rx_started;
	int rx_monitoring;

	int preamble_written;
	int has_msg;
	int last_edge_written;
	uint8_t out_msg[PD_BIT_LEN / 5];
	int verified_idx;
} pd_phy[PD_PORT_COUNT];

static const uint16_t enc4b5b[] = {
	0x1E, 0x09, 0x14, 0x15, 0x0A, 0x0B, 0x0E, 0x0F, 0x12, 0x13, 0x16,
	0x17, 0x1A, 0x1B, 0x1C, 0x1D};

/* Test utilities */

void pd_test_rx_set_preamble(int port, int has_preamble)
{
	pd_phy[port].has_preamble = has_preamble;
}

void pd_test_rx_msg_append_bits(int port, uint32_t bits, int nb)
{
	int i;

	for (i = 0; i < nb; ++i) {
		pd_phy[port].bits[pd_phy[port].total++] = bits & 1;
		bits >>= 1;
	}
}

void pd_test_rx_msg_append_kcode(int port, uint8_t kcode)
{
	pd_test_rx_msg_append_bits(port, kcode, 5);
}

void pd_test_rx_msg_append_sop(int port)
{
	pd_test_rx_msg_append_kcode(port, PD_SYNC1);
	pd_test_rx_msg_append_kcode(port, PD_SYNC1);
	pd_test_rx_msg_append_kcode(port, PD_SYNC1);
	pd_test_rx_msg_append_kcode(port, PD_SYNC2);
}

void pd_test_rx_msg_append_eop(int port)
{
	pd_test_rx_msg_append_kcode(port, PD_EOP);
}

void pd_test_rx_msg_append_4b(int port, uint8_t val)
{
	pd_test_rx_msg_append_bits(port, enc4b5b[val & 0xF], 5);
}

void pd_test_rx_msg_append_short(int port, uint16_t val)
{
	pd_test_rx_msg_append_4b(port, (val >> 0) & 0xF);
	pd_test_rx_msg_append_4b(port, (val >> 4) & 0xF);
	pd_test_rx_msg_append_4b(port, (val >> 8) & 0xF);
	pd_test_rx_msg_append_4b(port, (val >> 12) & 0xF);
}

void pd_test_rx_msg_append_word(int port, uint32_t val)
{
	pd_test_rx_msg_append_short(port, val & 0xFFFF);
	pd_test_rx_msg_append_short(port, val >> 16);
}

void pd_simulate_rx(int port)
{
	if (!pd_phy[port].rx_monitoring)
		return;
	pd_rx_start(port);
	pd_rx_disable_monitoring(port);
	pd_rx_event(port);
}

static int pd_test_tx_msg_verify(int port, uint8_t raw)
{
	int verified_idx = pd_phy[port].verified_idx++;
	return pd_phy[port].out_msg[verified_idx] == raw;
}

int pd_test_tx_msg_verify_kcode(int port, uint8_t kcode)
{
	return pd_test_tx_msg_verify(port, kcode);
}

int pd_test_tx_msg_verify_sop(int port)
{
	crc32_init();
	return pd_test_tx_msg_verify_kcode(port, PD_SYNC1) &&
	       pd_test_tx_msg_verify_kcode(port, PD_SYNC1) &&
	       pd_test_tx_msg_verify_kcode(port, PD_SYNC1) &&
	       pd_test_tx_msg_verify_kcode(port, PD_SYNC2);
}

int pd_test_tx_msg_verify_eop(int port)
{
	return pd_test_tx_msg_verify_kcode(port, PD_EOP);
}

int pd_test_tx_msg_verify_4b5b(int port, uint8_t b4)
{
	return pd_test_tx_msg_verify(port, enc4b5b[b4]);
}

int pd_test_tx_msg_verify_short(int port, uint16_t val)
{
	crc32_hash16(val);
	return pd_test_tx_msg_verify_4b5b(port, (val >> 0) & 0xF) &&
	       pd_test_tx_msg_verify_4b5b(port, (val >> 4) & 0xF) &&
	       pd_test_tx_msg_verify_4b5b(port, (val >> 8) & 0xF) &&
	       pd_test_tx_msg_verify_4b5b(port, (val >> 12) & 0xF);
}

int pd_test_tx_msg_verify_word(int port, uint32_t val)
{
	return pd_test_tx_msg_verify_short(port, val & 0xFFFF) &&
	       pd_test_tx_msg_verify_short(port, val >> 16);
}

int pd_test_tx_msg_verify_crc(int port)
{
	return pd_test_tx_msg_verify_word(port, crc32_result());
}


/* Mock functions */

void pd_init_dequeue(int port)
{
}

int pd_dequeue_bits(int port, int off, int len, uint32_t *val)
{
	int i;

	/* Rx must have started to receive message */
	ASSERT(pd_phy[port].rx_started);

	if (pd_phy[port].total <= off + len - PREAMBLE_OFFSET)
		return -1;
	*val = 0;
	for (i = 0; i < len; ++i)
		*val |= pd_phy[port].bits[off + i - PREAMBLE_OFFSET] << i;
	return off + len;
}

int pd_find_preamble(int port)
{
	return pd_phy[port].has_preamble ? PREAMBLE_OFFSET : -1;
}

int pd_write_preamble(int port)
{
	ASSERT(pd_phy[port].preamble_written == 0);
	pd_phy[port].preamble_written = 1;
	ASSERT(pd_phy[port].has_msg == 0);
	return 0;
}

static uint8_t decode_bmc(uint32_t val10)
{
	uint8_t ret = 0;
	int i;

	for (i = 0; i < 5; ++i)
		if (!!(val10 & (1 << (2 * i))) !=
		    !!(val10 & (1 << (2 * i + 1))))
			ret |= (1 << i);
	return ret;
}

int pd_write_sym(int port, int bit_off, uint32_t val10)
{
	pd_phy[port].out_msg[bit_off] = decode_bmc(val10);
	pd_phy[port].has_msg = 1;
	return bit_off + 1;
}

int pd_write_last_edge(int port, int bit_off)
{
	pd_phy[port].last_edge_written = 1;
	return bit_off;
}

void pd_dump_packet(int port, const char *msg)
{
	/* Not implemented */
}

void pd_tx_set_circular_mode(int port)
{
	/* Not implemented */
}

void pd_start_tx(int port, int polarity, int bit_len)
{
	ASSERT(pd_phy[port].hw_init_done);
	pd_phy[port].has_msg = 0;
	pd_phy[port].preamble_written = 0;
	pd_phy[port].verified_idx = 0;

	/*
	 * Hand over to test runner. The test runner must wake us after
	 * processing the packet.
	 */
	task_wake(TASK_ID_TEST_RUNNER);
	task_wait_event(-1);
}

void pd_tx_done(int port, int polarity)
{
	/* Nothing to do */
}

void pd_rx_start(int port)
{
	ASSERT(pd_phy[port].hw_init_done);
	pd_phy[port].rx_started = 1;
}

void pd_rx_complete(int port)
{
	ASSERT(pd_phy[port].hw_init_done);
	pd_phy[port].rx_started = 0;
}

int pd_rx_started(int port)
{
	return pd_phy[port].rx_started;
}

void pd_rx_enable_monitoring(int port)
{
	ASSERT(pd_phy[port].hw_init_done);
	pd_phy[port].rx_monitoring = 1;
}

void pd_rx_disable_monitoring(int port)
{
	ASSERT(pd_phy[port].hw_init_done);
	pd_phy[port].rx_monitoring = 0;
}

void pd_hw_release(int port)
{
	pd_phy[port].hw_init_done = 0;
}

void pd_hw_init(int port)
{
	pd_phy[port].hw_init_done = 1;
}

void pd_set_clock(int port, int freq)
{
	/* Not implemented */
}
