/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * STM32 bootloader protocol over I2C (AN4221).
 */

#include "common.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "watchdog.h"
#include "stm32_bl.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)

/* Timeouts */
#define ACK_POLL_INTERVAL_US	10000	/* 10 ms between ACK polls */
#define ACK_TIMEOUT_MS		2000	/* 2 s default */
#define ERASE_TIMEOUT_MS	35000	/* 35 s for mass erase */
#define WRITE_TIMEOUT_MS	2000	/* 2 s for block write */

/*
 * I2C GET reply lengths vary by bootloader version.
 * This is the expected total frame size for the data portion
 * (N+2 bytes where N is the first byte).
 */
static const struct {
	uint8_t ver;
	int len;
} get_reply_lens[] = {
	{ 0x10, 11 },
	{ 0x11, 17 },
	{ 0x12, 18 },
	{ 0, 0 }
};

/* Select the newer (higher) of two command codes, ignoring CMD_ERR */
static uint8_t newer_cmd(uint8_t prev, uint8_t a)
{
	if (prev == STM32_BL_CMD_ERR)
		return a;
	return (prev > a) ? prev : a;
}

/*
 * Poll for ACK from the STM32 bootloader.
 *
 * The bootloader may respond with:
 *   0x79 (ACK)  — success
 *   0x1F (NACK) — command rejected
 *   0x76 (BUSY) — still processing, keep polling
 *   0x00        — SDA held low (clock stretching / not ready)
 *   0xFF        — SDA high (no response yet)
 *   I2C error   — target not responding (NAK at address phase)
 */
static int stm32_bl_get_ack(int port, uint16_t addr, int timeout_ms)
{
	timestamp_t deadline = get_time();

	deadline.val += (uint64_t)timeout_ms * 1000;

	while (get_time().val < deadline.val) {
		uint8_t byte;
		int rv = i2c_xfer(port, addr, NULL, 0, &byte, 1);

		if (rv == EC_SUCCESS) {
			if (byte == STM32_BL_ACK)
				return EC_SUCCESS;
			if (byte == STM32_BL_NACK)
				return EC_ERROR_UNKNOWN;
			/* BUSY, 0x00, 0xFF — keep polling */
		}
		/* I2C error or non-terminal response — keep polling */

		watchdog_reload();
		crec_usleep(ACK_POLL_INTERVAL_US);
	}

	CPRINTS("STM32 BL: ACK timeout (%d ms)", timeout_ms);
	return EC_ERROR_TIMEOUT;
}

/* Send a bootloader command: {cmd, cmd ^ 0xFF}, then wait for ACK */
static int stm32_bl_send_cmd(int port, uint16_t addr, uint8_t cmd)
{
	uint8_t buf[2] = { cmd, cmd ^ 0xFF };
	int rv;

	rv = i2c_xfer(port, addr, buf, 2, NULL, 0);
	if (rv)
		return rv;

	return stm32_bl_get_ack(port, addr, ACK_TIMEOUT_MS);
}

/* Send a 4-byte address with XOR checksum */
static int stm32_bl_send_addr(int port, uint16_t addr, uint32_t flash_addr)
{
	uint8_t buf[5];

	buf[0] = flash_addr >> 24;
	buf[1] = (flash_addr >> 16) & 0xFF;
	buf[2] = (flash_addr >> 8) & 0xFF;
	buf[3] = flash_addr & 0xFF;
	buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];

	int rv = i2c_xfer(port, addr, buf, 5, NULL, 0);

	if (rv)
		return rv;

	return stm32_bl_get_ack(port, addr, ACK_TIMEOUT_MS);
}

int stm32_bl_init(int i2c_port, uint16_t addr, struct stm32_bl_dev *dev)
{
	static uint8_t buf[257];
	int rv, i, n;

	memset(dev, 0xFF, sizeof(*dev));
	dev->cmd_wm = STM32_BL_CMD_ERR;
	dev->cmd_er = STM32_BL_CMD_ERR;
	dev->cmd_rm = STM32_BL_CMD_ERR;
	dev->cmd_go = STM32_BL_CMD_ERR;

	/* GVR: Get Version */
	rv = stm32_bl_send_cmd(i2c_port, addr, STM32_BL_CMD_GVR);
	if (rv) {
		CPRINTS("STM32 BL: GVR failed — is STM32 in bootloader?");
		return rv;
	}
	/* I2C: 1 data byte (version), then trailing ACK */
	rv = i2c_xfer(i2c_port, addr, NULL, 0, buf, 1);
	if (rv)
		return rv;
	dev->version = buf[0];
	rv = stm32_bl_get_ack(i2c_port, addr, ACK_TIMEOUT_MS);
	if (rv)
		return rv;
	CPRINTS("STM32 BL: version 0x%02x", dev->version);

	/* GET: supported commands */
	int get_len = 17; /* default frame data length */

	for (i = 0; get_reply_lens[i].ver; i++) {
		if (get_reply_lens[i].ver == dev->version) {
			get_len = get_reply_lens[i].len;
			break;
		}
	}

	rv = stm32_bl_send_cmd(i2c_port, addr, STM32_BL_CMD_GET);
	if (rv)
		return rv;
	/* I2C frame: [N] [N+1 data bytes] — total N+2 bytes */
	rv = i2c_xfer(i2c_port, addr, NULL, 0, buf, get_len + 2);
	if (rv)
		return rv;
	rv = stm32_bl_get_ack(i2c_port, addr, ACK_TIMEOUT_MS);
	if (rv)
		return rv;

	n = buf[0] + 1;
	dev->bl_version = buf[1];

	for (i = 1; i < n; i++) {
		uint8_t val = buf[i + 1];

		switch (val) {
		case STM32_BL_CMD_RM:
			dev->cmd_rm = val;
			break;
		case STM32_BL_CMD_GO:
			dev->cmd_go = val;
			break;
		case STM32_BL_CMD_WM:
		case STM32_BL_CMD_WM_NS:
			dev->cmd_wm = newer_cmd(dev->cmd_wm, val);
			break;
		case STM32_BL_CMD_ER:
		case STM32_BL_CMD_EE:
		case STM32_BL_CMD_EE_NS:
			dev->cmd_er = newer_cmd(dev->cmd_er, val);
			break;
		}
	}

	/* GID: device ID */
	rv = stm32_bl_send_cmd(i2c_port, addr, STM32_BL_CMD_GID);
	if (rv)
		return rv;
	rv = i2c_xfer(i2c_port, addr, NULL, 0, buf, 3);
	if (rv)
		return rv;
	rv = stm32_bl_get_ack(i2c_port, addr, ACK_TIMEOUT_MS);
	if (rv)
		return rv;

	n = buf[0] + 1;
	dev->pid = (n >= 2) ? ((buf[1] << 8) | buf[2]) : buf[1];

	CPRINTS("STM32 BL: PID 0x%04x, BL %d.%d, WM=0x%02x ER=0x%02x",
		dev->pid, dev->bl_version >> 4, dev->bl_version & 0xF,
		dev->cmd_wm, dev->cmd_er);

	return EC_SUCCESS;
}

int stm32_bl_mass_erase(int i2c_port, uint16_t addr,
			const struct stm32_bl_dev *dev)
{
	if (dev->cmd_er == STM32_BL_CMD_ERR) {
		CPRINTS("STM32 BL: erase command not supported");
		return EC_ERROR_UNIMPLEMENTED;
	}

	int rv = stm32_bl_send_cmd(i2c_port, addr, dev->cmd_er);

	if (rv)
		return rv;

	if (dev->cmd_er == STM32_BL_CMD_ER) {
		/* Regular erase (0x43): 0xFF = mass erase, + complement */
		uint8_t buf[2] = { 0xFF, 0x00 };

		rv = i2c_xfer(i2c_port, addr, buf, 2, NULL, 0);
	} else {
		/* Extended erase (0x44/0x45): 0xFFFF = mass erase + checksum */
		uint8_t buf[3] = { 0xFF, 0xFF, 0x00 };

		rv = i2c_xfer(i2c_port, addr, buf, 3, NULL, 0);
	}
	if (rv)
		return rv;

	return stm32_bl_get_ack(i2c_port, addr, ERASE_TIMEOUT_MS);
}

int stm32_bl_write_memory(int i2c_port, uint16_t addr,
			  const struct stm32_bl_dev *dev,
			  uint32_t flash_addr, const uint8_t *data, int len)
{
	static uint8_t buf[258]; /* max: 1 (len) + 256 (data) + 1 (checksum) */
	int aligned_len, i, rv;
	uint8_t cs;

	if (dev->cmd_wm == STM32_BL_CMD_ERR)
		return EC_ERROR_UNIMPLEMENTED;
	if (len <= 0 || len > STM32_BL_MAX_DATA)
		return EC_ERROR_INVAL;
	if (flash_addr & 3)
		return EC_ERROR_INVAL;

	/* Send write command */
	rv = stm32_bl_send_cmd(i2c_port, addr, dev->cmd_wm);
	if (rv)
		return rv;

	/* Send address + checksum */
	rv = stm32_bl_send_addr(i2c_port, addr, flash_addr);
	if (rv)
		return rv;

	/* Build data frame: [aligned_len-1] [data...] [padding 0xFF] [cs] */
	aligned_len = (len + 3) & ~3;
	cs = aligned_len - 1;
	buf[0] = aligned_len - 1;

	for (i = 0; i < len; i++) {
		cs ^= data[i];
		buf[i + 1] = data[i];
	}
	for (i = len; i < aligned_len; i++) {
		cs ^= 0xFF;
		buf[i + 1] = 0xFF;
	}
	buf[aligned_len + 1] = cs;

	rv = i2c_xfer(i2c_port, addr, buf, aligned_len + 2, NULL, 0);
	if (rv)
		return rv;

	return stm32_bl_get_ack(i2c_port, addr, WRITE_TIMEOUT_MS);
}

int stm32_bl_read_memory(int i2c_port, uint16_t addr,
			 const struct stm32_bl_dev *dev,
			 uint32_t flash_addr, uint8_t *data, int len)
{
	int rv;

	if (dev->cmd_rm == STM32_BL_CMD_ERR)
		return EC_ERROR_UNIMPLEMENTED;

	if (len <= 0 || len > STM32_BL_MAX_DATA)
		return EC_ERROR_INVAL;

	/* Send read command */
	rv = stm32_bl_send_cmd(i2c_port, addr, dev->cmd_rm);
	if (rv)
		return rv;

	/* Send address + checksum */
	rv = stm32_bl_send_addr(i2c_port, addr, flash_addr);
	if (rv)
		return rv;

	/* Send length as command: {len-1, (len-1)^0xFF} */
	rv = stm32_bl_send_cmd(i2c_port, addr, len - 1);
	if (rv)
		return rv;

	/* Read data */
	rv = i2c_xfer(i2c_port, addr, NULL, 0, data, len);
	return rv;
}

int stm32_bl_go(int i2c_port, uint16_t addr,
		const struct stm32_bl_dev *dev, uint32_t go_addr)
{
	if (dev->cmd_go == STM32_BL_CMD_ERR)
		return EC_ERROR_UNIMPLEMENTED;

	int rv = stm32_bl_send_cmd(i2c_port, addr, dev->cmd_go);

	if (rv)
		return rv;

	rv = stm32_bl_send_addr(i2c_port, addr, go_addr);
	/* GO ACK may fail if device resets immediately — that's OK */
	return EC_SUCCESS;
}
