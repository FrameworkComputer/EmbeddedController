/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Megachips DisplayPort to HDMI protocol converter / level shifter driver.
 */

#include "config.h"
#include "console.h"
#include "common.h"
#include "ec_commands.h"
#include "mcdp28x0.h"
#include "stream_adaptor.h"
#include "timer.h"
#include "usart-stm32f0.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

static uint8_t mcdp_inbuf[MCDP_INBUF_MAX];

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

struct usart_config const usart_mcdp;

IO_STREAM_CONFIG(usart_mcdp, MCDP_INBUF_MAX, MCDP_OUTBUF_MAX, NULL, NULL);

USART_CONFIG(usart_mcdp,
	     CONFIG_MCDP28X0,
	     115200,
	     usart_mcdp_rx_queue,
	     usart_mcdp_tx_queue);

/**
 * Compute checksum.
 *
 * @seed initial value of checksum.
 * @msg message bytes to compute checksum on.
 * @cnt count of message bytes.
 * @return partial checksum.
 */
static uint8_t compute_checksum(uint8_t seed, const uint8_t *msg, int cnt)
{
	int i;
	uint8_t chksum = seed;

	for (i = 0; i < cnt; i++)
		chksum += msg[i];
	return ~chksum + 1;
}

/**
 * transmit message over serial
 *
 * Packet consists of:
 *   msg[0]     == length of entire packet
 *   msg[1]     == 1st message byte (typically command)
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
	/* 1st byte (not in msg) is always cnt + 2, so seed chksum with that */
	uint8_t chksum = compute_checksum(cnt + 2, msg, cnt);

	if (out_stream_write(&usart_mcdp_out.out, &out, 1) != 1)
		return EC_ERROR_UNKNOWN;

	if (out_stream_write(&usart_mcdp_out.out, msg, cnt) != cnt)
		return EC_ERROR_UNKNOWN;

	if (out_stream_write(&usart_mcdp_out.out, &chksum, 1) != 1)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

/**
 * receive message over serial
 *
 * While definitive documentation is lacking its believed the following receive
 * packet is always true.
 *
 * Packet consists of:
 *   msg[0]     == length of entire packet
 *   msg[1]     == 1st message byte (typically command)
 *   msg[cnt+2] == last message byte
 *   msg[cnt+3] == checksum
 *
 * @msg pointer to buffer to read message into
 * @cnt count of message bytes
 * @return zero if success, error code otherwise
 */
static int rx_serial(uint8_t *msg, int cnt)
{
	size_t read;
	int retry = 2;

	read = in_stream_read(&usart_mcdp_in.in, msg, cnt);
	while ((read < cnt) && retry) {
		usleep(100*MSEC);
		read += in_stream_read(&usart_mcdp_in.in, msg + read,
				       cnt - read);
		retry--;
	}

	print_buffer(msg, cnt);

	/* Some response sizes are dynamic so shrink cnt accordingly. */
	if (cnt > msg[0])
		cnt = msg[0];

	if (msg[cnt-1] != compute_checksum(0, msg, cnt-1))
		return EC_ERROR_UNKNOWN;

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
	const uint8_t msg[2] = {MCDP_CMD_GETINFO, 0x00}; /* cmd + msg type */

	if (tx_serial(msg, sizeof(msg)))
		return EC_ERROR_UNKNOWN;

	if (rx_serial(mcdp_inbuf, MCDP_RSP_LEN(MCDP_LEN_GETINFO)))
		return EC_ERROR_UNKNOWN;

	memcpy(info, &mcdp_inbuf[2], MCDP_LEN_GETINFO);

	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_MCDP
static int mcdp_get_dev_id(char *dev, uint8_t dev_id, int dev_cnt)
{
	uint8_t msg[2];
	msg[0] = MCDP_CMD_GETDEVID;
	msg[1] = dev_id;

	if (tx_serial(msg, sizeof(msg)))
		return EC_ERROR_UNKNOWN;

	if (rx_serial(mcdp_inbuf, sizeof(mcdp_inbuf)))
		return EC_ERROR_UNKNOWN;

	memcpy(dev, &mcdp_inbuf[2], mcdp_inbuf[0] - 3);
	dev[mcdp_inbuf[0] - 3] = '\0';
	return EC_SUCCESS;
}

int command_mcdp(int argc, char **argv)
{
	int rv = EC_SUCCESS;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	mcdp_enable();
	if (!strncasecmp(argv[1], "info", 4)) {
		struct mcdp_info info;
		rv = mcdp_get_info(&info);
		if (!rv)
			ccprintf("family:%04x chipid:%04x irom:%d.%d.%d "
				 "fw:%d.%d.%d\n",
				 MCDP_FAMILY(info.family),
				 MCDP_CHIPID(info.chipid),
				 info.irom.major, info.irom.minor,
				 info.irom.build,
				 info.fw.major, info.fw.minor, info.fw.build);
	} else if (!strncasecmp(argv[1], "devid", 4)) {
		uint8_t dev_id = strtoi(argv[2], &e, 10);
		char dev[32];
		if (*e)
			rv = EC_ERROR_PARAM2;
		else
			rv = mcdp_get_dev_id(dev, dev_id, 32);
		if (!rv)
			ccprintf("devid[%d] = %s\n", dev_id, dev);
	} else {
		rv = EC_ERROR_PARAM1;
	}

	mcdp_disable();
	return rv;
}
DECLARE_CONSOLE_COMMAND(mcdp, command_mcdp,
			"info|devid <id>",
			"USB PD",
			NULL);
#endif /* CONFIG_CMD_MCDP */
