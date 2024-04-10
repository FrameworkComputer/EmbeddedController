/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Megachips DisplayPort to HDMI protocol converter / level shifter driver.
 */

#include "common.h"
#include "config.h"
#include "console.h"
#include "ec_commands.h"
#include "mcdp28x0.h"
#include "queue.h"
#include "queue_policies.h"
#include "timer.h"
#include "usart-stm32f0.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

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
static inline void print_buffer(uint8_t *buf, int cnt)
{
}
#endif

static struct usart_config const usart_mcdp;

struct queue const usart_mcdp_rx_queue = QUEUE_DIRECT(
	MCDP_INBUF_MAX, uint8_t, usart_mcdp.producer, null_consumer);
struct queue const usart_mcdp_tx_queue = QUEUE_DIRECT(
	MCDP_OUTBUF_MAX, uint8_t, null_producer, usart_mcdp.consumer);

static struct usart_config const usart_mcdp =
	USART_CONFIG(CONFIG_MCDP28X0, usart_rx_interrupt, usart_tx_interrupt,
		     115200, 0, usart_mcdp_rx_queue, usart_mcdp_tx_queue);

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

	if (queue_add_unit(&usart_mcdp_tx_queue, &out) != 1)
		return MCDP_ERROR_TX_CNT;

	if (queue_add_units(&usart_mcdp_tx_queue, msg, cnt) != cnt)
		return MCDP_ERROR_TX_BODY;

	print_buffer((uint8_t *)msg, cnt);

	if (queue_add_unit(&usart_mcdp_tx_queue, &chksum) != 1)
		return MCDP_ERROR_TX_CHKSUM;

	return MCDP_SUCCESS;
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

	read = queue_remove_units(&usart_mcdp_rx_queue, msg, cnt);
	while ((read < cnt) && retry) {
		crec_usleep(100 * MSEC);
		read += queue_remove_units(&usart_mcdp_rx_queue, msg + read,
					   cnt - read);
		retry--;
	}

	print_buffer(msg, cnt);

	/* Some response sizes are dynamic so shrink cnt accordingly. */
	if (cnt > msg[0])
		cnt = msg[0];

	if (msg[cnt - 1] != compute_checksum(0, msg, cnt - 1))
		return MCDP_ERROR_CHKSUM;

	if (read != cnt) {
		CPRINTF("rx_serial: read bytes %d != %d cnt\n", read, cnt);
		return MCDP_ERROR_RX_BYTES;
	}

	return MCDP_SUCCESS;
}

static int rx_serial_ack(void)
{
	int rv = rx_serial(mcdp_inbuf, 3);
	if (rv)
		return rv;

	if (mcdp_inbuf[1] != MCDP_CMD_ACK)
		return MCDP_ERROR_RX_ACK;

	return rv;
}

void mcdp_enable(void)
{
	usart_init(&usart_mcdp);
}

void mcdp_disable(void)
{
	usart_shutdown(&usart_mcdp);
}

int mcdp_get_info(struct mcdp_info *info)
{
	const uint8_t msg[2] = { MCDP_CMD_APPSTEST, 0x28 };
	int rv = tx_serial(msg, sizeof(msg));

	if (rv)
		return rv;

	rv = rx_serial_ack();
	if (rv)
		return rv;

	/* chksum is unreliable ... don't check */
	rx_serial(mcdp_inbuf, MCDP_RSP_LEN(MCDP_LEN_GETINFO));

	memcpy(info, &mcdp_inbuf[2], MCDP_LEN_GETINFO);

	return rv;
}

#ifdef CONFIG_CMD_MCDP
static int mcdp_get_dev_id(char *dev, uint8_t dev_id, int dev_cnt)
{
	uint8_t msg[2];
	int rv;
	msg[0] = MCDP_CMD_GETDEVID;
	msg[1] = dev_id;

	rv = tx_serial(msg, sizeof(msg));
	if (rv)
		return rv;

	rv = rx_serial(mcdp_inbuf, sizeof(mcdp_inbuf));
	if (rv)
		return rv;

	memcpy(dev, &mcdp_inbuf[2], mcdp_inbuf[0] - 3);
	dev[mcdp_inbuf[0] - 3] = '\0';
	return EC_SUCCESS;
}

static int mcdp_appstest(uint8_t cmd, int paramc, char **paramv)
{
	uint8_t msg[6];
	char *e;
	int i;
	int rv = MCDP_SUCCESS;

	/* setup any appstest params */
	msg[0] = MCDP_CMD_APPSTESTPARAM;
	for (i = 0; i < paramc; i++) {
		uint32_t param = strtoi(paramv[i], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
		msg[1] = i + 1;
		msg[2] = (param >> 24) & 0xff;
		msg[3] = (param >> 16) & 0xff;
		msg[4] = (param >> 8) & 0xff;
		msg[5] = (param >> 0) & 0xff;
		rv = tx_serial(msg, sizeof(msg));
		if (rv)
			return rv;

		rv = rx_serial_ack();
		if (rv)
			return rv;
	}

	msg[0] = MCDP_CMD_APPSTEST;
	msg[1] = cmd;
	rv = tx_serial(msg, 2);
	if (rv)
		return rv;

	rv = rx_serial_ack();
	if (rv)
		return rv;

	/* magic */
	rx_serial(mcdp_inbuf, sizeof(mcdp_inbuf));
	rx_serial(mcdp_inbuf, sizeof(mcdp_inbuf));

	return EC_SUCCESS;
}

int command_mcdp(int argc, const char **argv)
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
				 MCDP_CHIPID(info.chipid), info.irom.major,
				 info.irom.minor, info.irom.build,
				 info.fw.major, info.fw.minor, info.fw.build);
	} else if (!strncasecmp(argv[1], "devid", 4)) {
		uint8_t dev_id = strtoi(argv[2], &e, 10);
		char dev[32];
		if (*e)
			return EC_ERROR_PARAM2;
		else
			rv = mcdp_get_dev_id(dev, dev_id, 32);
		if (!rv)
			ccprintf("devid[%d] = %s\n", dev_id, dev);
	} else if (!strncasecmp(argv[1], "appstest", 4)) {
		uint8_t cmd = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM2;
		else
			rv = mcdp_appstest(cmd, argc - 3, &argv[3]);
		if (!rv)
			ccprintf("appstest[%d] completed\n", cmd);
	} else {
		return EC_ERROR_PARAM1;
	}

	mcdp_disable();
	if (rv)
		ccprintf("mcdp_error:%d\n", rv);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mcdp, command_mcdp,
			"info|devid <id>|appstest <cmd> [<params>]", "USB PD");
#endif /* CONFIG_CMD_MCDP */
