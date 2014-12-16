/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Megachips DisplayPort to HDMI protocol converter / level shifter driver.
 */

#include "config.h"
#include "console.h"
#include "common.h"
#include "mcdp28x0.h"
#include "timer.h"
#include "usart-stm32f0.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

#undef MCDP_DEBUG

#ifdef MCDP_DEBUG
static inline void print_buffer(uint8_t *buf, int cnt)
{
	int i;
	CPRINTF("buf:");
	for (i = 0; i < cnt; i++) {
		if (i && !(i % 4))
			CPRINTF("\n    ");
		CPRINTF("[%02d]0x%02x ", i, buf[i]);
	}
	CPRINTF("\n");
}
#else
static inline void print_buffer(uint8_t *buf, int cnt) {}
#endif

USART_CONFIG(usart_mcdp, CONFIG_MCDP28X0, 115200, MCDP_INBUF_MAX,
	     MCDP_OUTBUF_MAX, NULL, NULL);

/**
 * Compute checksum.
 *
 * @msg message bytes to compute checksum on.
 * @cnt count of message bytes.
 * @return partial checksum.
 */
static uint8_t compute_checksum(const uint8_t *msg, int cnt)
{
	int i;
	/* 1st byte (not in msg) is always cnt + 2, so seed chksum with that */
	uint8_t chksum = cnt + 2;

	for (i = 0; i < cnt; i++)
		chksum += msg[i];
	return ~chksum + 1;
}

/**
 * transmit message over serial
 *
 * Packet consists of:
 *   msg[0]     == length including this byte + cnt + chksum
 *   msg[1]     == 1st message byte
 *   msg[cnt+1] == last message byte
 *   msg[cnt+2] == checksum
 *
 * @msg message bytes
 * @cnt count of message bytes
 * @return zero if success, error code otherwise
 */
static int tx_serial(const uint8_t *msg, int cnt)
{
	uint8_t out = cnt + 2;
	uint8_t chksum = compute_checksum(msg, cnt);

	if (out_stream_write(&usart_mcdp.out, &out, 1) != 1)
		return EC_ERROR_UNKNOWN;

	if (out_stream_write(&usart_mcdp.out, msg, cnt) != cnt)
		return EC_ERROR_UNKNOWN;

	if (out_stream_write(&usart_mcdp.out, &chksum, 1) != 1)
		return EC_ERROR_UNKNOWN;

	print_buffer(usart_mcdp_tx_buffer, cnt + 2);

	return EC_SUCCESS;
}

/**
 * receive message over serial
 *
 * @msg pointer to buffer to read message into
 * @cnt count of message bytes
 * @return zero if success, error code otherwise
 */
static int rx_serial(uint8_t *msg, int cnt)
{
	size_t read;
	int retry = 2;

	read = in_stream_read(&usart_mcdp.in, msg, cnt);
	while ((read < cnt) && retry) {
		usleep(100*MSEC);
		read += in_stream_read(&usart_mcdp.in, msg + read,
				       cnt - read);
		retry--;
	}

	print_buffer(msg, cnt);

	return !(read == cnt);
}

void mcdp_enable(void)
{
	usart_init(&usart_mcdp);
}

void mcdp_disable(void)
{
	usart_shutdown(&usart_mcdp);
}

int mcdp_get_info(struct mcdp_info  *info)
{
	uint8_t inbuf[MCDP_RSP_LEN(MCDP_LEN_GETINFO)];
	const uint8_t msg[2] = {MCDP_CMD_GETINFO, 0x00}; /* cmd + msg type */

	if (tx_serial(msg, sizeof(msg)))
		return EC_ERROR_UNKNOWN;

	if (rx_serial(inbuf, sizeof(inbuf)))
		return EC_ERROR_UNKNOWN;

	/* check length & returned command ... no checksum provided */
	if (((inbuf[0] - 1) != sizeof(inbuf)) ||
	    (inbuf[1] != MCDP_CMD_GETINFO))
		return EC_ERROR_UNKNOWN;

	memcpy(info, &inbuf[2], MCDP_LEN_GETINFO);

#ifdef MCDP_DEBUG
	CPRINTF("family:%04x chipid:%04x irom:%d.%d.%d fw:%d.%d.%d\n",
		MCDP_FAMILY(info->family), MCDP_CHIPID(info->chipid),
		info->irom.major, info->irom.minor, info->irom.build,
		info->fw.major, info->fw.minor, info->fw.build);
#endif
	return EC_SUCCESS;
}
