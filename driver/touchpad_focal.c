/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "sha256.h"
#include "shared_mem.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "touchpad.h"
#include "update_fw.h"
#include "usb_api.h"
#include "usb_hid_touchpad.h"
#include "util.h"
#include "watchdog.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/byteorder.h>

/* Console output macros */
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ##args)

#define TASK_EVENT_POWER TASK_EVENT_CUSTOM_BIT(0)
#define TASK_EVENT_TP_UPDATED TASK_EVENT_CUSTOM_BIT(1)

#if DT_HAS_COMPAT_STATUS_OKAY(focaltech_fts)

#define FTS_TP_NODE DT_INST(0, focaltech_fts)

#define GPIO_TOUCHPAD_INT \
	GPIO_SIGNAL(DT_PROP(DT_PROP(FTS_TP_NODE, irq), irq_pin))

#define CONFIG_TOUCHPAD_I2C_ADDR_FLAGS DT_REG_ADDR(FTS_TP_NODE)
#define CONFIG_TOUCHPAD_I2C_PORT I2C_PORT_BY_DEV(FTS_TP_NODE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(focaltech_fts) */

#define FTS_CHIP_ID_H 0x54
#define FTS_CHIP_ID_L 0x52

#define FTS_BOOT_ID_H 0x54
#define FTS_BOOT_ID_L 0x5E
#define FTS_VENDOR_ID 0x2808
#define FTS_PRODUCT_ID 0x0364

#define FTS_REG_PID_H 0xE4
#define FTS_REG_PID_L 0xE5
#define FTS_REG_UPGRADE 0xFC
#define FTS_UPGRADE_AA 0xAA
#define FTS_UPGRADE_55 0x55
#define FTS_DELAY_UPGRADE_AA 10
#define FTS_DELAY_UPGRADE_RESET 80
#define FTS_UPGRADE_LOOP 10
#define FTS_CMD_ERASE_APP 0x61
#define FTS_CMD_FLASH_STATUS_ECC_OK 0xF055
#define FTS_CMD_FLASH_STATUS_ERASE_OK 0xF0AA
#define FTS_RETRIES_REASE 50
#define FTS_RETRIES_DELAY_REASE 400
#define FTS_CMD_FLASH_STATUS 0x6A
#define FTS_CMD_WRITE 0xBF
#define FTS_REG_CHIP_ID_H 0xA3
#define FTS_REG_FW_VER 0xA6
#define FTS_REG_CHIP_ID_L 0x9F
#define FTS_REG_BOOT_ID 0x90
#define FTS_CMD_ECC_INIT 0x64
#define FTS_CMD_ECC_CAL 0x65
#define FTS_CMD_ECC_READ 0x66
#define FTS_RETRIES_ECC_CAL 10
#define FTS_RETRIES_DELAY_ECC_CAL 50
#define FTS_REG_POWER_MODE 0xA5
#define FTS_CMD_FLASH_MODE 0x09
#define FLASH_MODE_WRITE_FLASH_VALUE 0x0A
#define FLASH_MODE_UPGRADE_VALUE 0x0B
#define FTS_CMD_APP_DATA_LEN_INCELL 0x7A
#define FTS_CMD_DATA_LEN 0xB0
#define FTS_CMD_DATA_LEN_LEN 4
#define FTS_DELAY_ERASE_PAGE 6
#define FTS_SIZE_PAGE 256
#define FTS_FLASH_PACKET_SIZE 128 // 1024//max 2048
#define AL2_FCS_COEF ((1 << 15) + (1 << 10) + (1 << 3))

#define FTS_TOUCH_DOWN 0
#define FTS_TOUCH_UP 1
#define FTS_TOUCH_CONTACT 2
#define EVENT_DOWN(flag) \
	((FTS_TOUCH_DOWN == flag) || (FTS_TOUCH_CONTACT == flag))
#define EVENT_UP(flag) (FTS_TOUCH_UP == flag)

#define BYTE_OFF_0(x) (uint8_t)((x) & 0xFF)
#define BYTE_OFF_8(x) (uint8_t)(((x) >> 8) & 0xFF)
#define BYTE_OFF_16(x) (uint8_t)(((x) >> 16) & 0xFF)
#define BYTE_OFF_24(x) (uint8_t)(((x) >> 24) & 0xFF)

static uint32_t irq_ts;

/*****************************************************************************
 * functions body
 *****************************************************************************/
static int fts_read(uint8_t *cmd, uint32_t cmdlen, uint8_t *data,
		    uint32_t datalen)
{
	int ret = 0;

	ret = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR_FLAGS,
		       cmd, cmdlen, data, datalen);
	if (ret < 0) {
		CPRINTS("i2c_xfer(read) fail,ret:%d", ret);
	}

	return ret;
}

static int fts_write(uint8_t *writebuf, uint32_t writelen)
{
	int ret = 0;

	ret = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR_FLAGS,
		       writebuf, writelen, NULL, 0);
	if (ret < 0) {
		CPRINTS("i2c_xfer(write) fail,ret:%d", ret);
	}

	return ret;
}

static int fts_read_reg(uint8_t addr, uint8_t *value)
{
	return fts_read(&addr, 1, value, 1);
}

static int fts_write_reg(uint8_t addr, uint8_t value)
{
	uint8_t buf[2] = { 0 };

	buf[0] = addr;
	buf[1] = value;
	return fts_write(buf, sizeof(buf));
}

static int fts_hid2std(int mode)
{
	int ret = 0;
	uint8_t buf[3] = { 0xEB, 0xAA, 0x09 };
	uint8_t val[3] = { 0 };

	if (mode == 1) {
		/* Don't need delay */
		ret = fts_read(buf, 3, val, 3);
		if (ret < 0) {
			CPRINTS("send hid2std cmd failed");
			return ret;
		}
	} else {
		ret = fts_write(buf, 3);
		if (ret < 0) {
			CPRINTS("hid2std cmd write fail");
			return ret;
		}

		crec_msleep(10);
		ret = fts_read(NULL, 0, val, 3);
		if (ret < 0) {
			CPRINTS("hid2std cmd read fail");
			return ret;
		}
	}

	if ((0xEB == val[0]) && (0xAA == val[1]) && (0x08 == val[2])) {
		CPRINTS("hidi2c change to stdi2c successful");
	} else {
		CPRINTS("hidi2c change to stdi2c not support or fail");
	}
	return 0;
}

static void fts_tp_set_power(int enable)
{
	CPRINTS("focal TP power %s", enable ? "on" : "off");

	if (enable) {
		fts_write_reg(FTS_REG_POWER_MODE, 0x0);
	} else {
		fts_write_reg(FTS_REG_POWER_MODE, 0x03);
	}
}

static int fts_tp_read_report(void)
{
	int rv;
	int i, ri;
	struct usb_hid_touchpad_report report;
	uint16_t timestamp;
	uint8_t cmd = 0x01;
	uint8_t touch_buf[64] = { 0 };
	int touch_size = 5 * 6 + 2;
	int offset = 0;
	int point_id = 0;
	int event_flag = 0;
	int pressure = 0;

	/* Compute and save timestamp early in case another interrupt comes. */
	timestamp = irq_ts / USB_HID_TOUCHPAD_TIMESTAMP_UNIT;

	rv = fts_read(&cmd, 1, touch_buf, touch_size);

	if (rv) {
		CPRINTS("read report error (%d)", rv);
		return rv;
	}

	memset(&report, 0, sizeof(report));
	report.id = 0x01;
	ri = 0; /* Next finger index in HID report */

	for (i = 0; i < 5; i++) {
		offset = i * 6;
		point_id = touch_buf[4 + offset] >> 4; // i;
		event_flag = touch_buf[2 + offset] >> 6;
		if (point_id >= 5) {
			break;
		}
		pressure = (touch_buf[6 + offset] & 0xFF);
		if (pressure < 25) {
			pressure = 25;
		} else if (pressure >= 255) {
			pressure = 281;
		}
		if (EVENT_DOWN(event_flag)) {
			report.finger[ri].id = point_id;
			report.finger[ri].confidence = 1;
			report.finger[ri].tip = 1;
			report.finger[ri].inrange = 1;
			report.finger[ri].x =
				((touch_buf[2 + offset] & 0x0F) << 8) +
				(touch_buf[3 + offset] & 0xFF);
			report.finger[ri].y =
				((touch_buf[4 + offset] & 0x0F) << 8) +
				(touch_buf[5 + offset] & 0xFF);

			report.finger[ri].pressure = pressure;
			ri++;
		} else {
			report.finger[ri].id = point_id;
			/* When a finger is leaving, it's not a plam */
			report.finger[ri].confidence = 1;
			ri++;
		}
	}

	report.count = ri;
	report.timestamp = timestamp;
	if (report.count > 0) {
		report.button = (touch_buf[0] & 0xFF);
	}

	set_touchpad_report(&report);

	return 0;
}

static int fts_read_chip_id(void)
{
	int ret = 0;
	uint8_t val[2] = { 0 };
	int i = 0;
	uint8_t reg = FTS_REG_BOOT_ID;

	CPRINTS("%s", __func__);

	crec_msleep(100);
	for (i = 0; i < 5; i++) {
		ret = fts_read_reg(FTS_REG_CHIP_ID_H, &val[0]);
		if (ret < 0) {
			CPRINTS("fts_read_reg fail ret = %x", ret);
		}
		if (val[0] == FTS_CHIP_ID_H) {
			CPRINTS("read id %x", val[0]);
			return 0;
		}

		CPRINTS("read id %x", val[0]);
		crec_msleep(50);
	}

	CPRINTS("read chip ID fail, read boot id");
	for (i = 0; i < 5; i++) {
		ret = fts_read(&reg, 1, val, 2);
		if (ret < 0) {
			CPRINTS("fts_read_reg fail ret = %x", ret);
		}
		if (val[0] == FTS_BOOT_ID_H && val[1] == FTS_BOOT_ID_L) {
			CPRINTS("read id 0x%x%x", val[0], val[1]);
			return 0;
		}
		CPRINTS("read id 0x%x%x", val[0], val[1]);
	}

	return -1;
}

static bool fts_fw_is_valid(void)
{
	int ret = 0;
	uint8_t val = 0;
	int i = 0;

	CPRINTS("%s", __func__);

	for (i = 0; i < 5; i++) {
		ret = fts_read_reg(FTS_REG_CHIP_ID_H, &val);
		if (ret < 0) {
			CPRINTS("fts_read_reg fail ret = %x", ret);
		}
		if (val == FTS_CHIP_ID_H)
			break;

		CPRINTS("read id %x", val);
		crec_msleep(50);
	}

	if (i >= 5) {
		CPRINTS("read ID timeout");
		return false;
	}

	return true;
}

/* Initialize the controller ICs after reset */
test_export_static void fts_tp_init(void)
{
	int ret = 0;

	CPRINTS("%s", __func__);
	ret = fts_read_chip_id();
	if (ret < 0) {
		CPRINTS("not fts ic");
		return;
	}

	/* Enable interrupt to fetch reports */
	gpio_enable_interrupt(GPIO_TOUCHPAD_INT);

	return;
}
DECLARE_DEFERRED(fts_tp_init);

#if defined(CONFIG_USB_UPDATE) || defined(CONFIG_TEST)
int touchpad_get_info(struct touchpad_info *tp)
{
	uint8_t ver = 0;
	uint8_t pid_h = 0;
	uint8_t pid_l = 0;

	if (!fts_fw_is_valid()) {
		tp->status = EC_RES_SUCCESS;
		tp->vendor = FTS_VENDOR_ID;
		/*
		 * failed to get system info, FW corrupted, return some default
		 * values.
		 */
		tp->fts.id = FTS_PRODUCT_ID;
		tp->fts.fw_version = 0;
		tp->fts.fw_checksum = 0;
		return sizeof(*tp);
	}

	tp->status = EC_RES_SUCCESS;
	tp->vendor = FTS_VENDOR_ID;
	fts_read_reg(FTS_REG_PID_H, &pid_h);
	fts_read_reg(FTS_REG_PID_L, &pid_l);
	tp->fts.id = (pid_h << 8) + pid_l;
	fts_read_reg(FTS_REG_FW_VER, &ver);
	tp->fts.fw_version = ver;
	tp->fts.fw_checksum = 0;

	return sizeof(*tp);
}

static int fts_enter_upgrade_mode(void)
{
	int ret = 0;
	uint8_t reg = FTS_REG_UPGRADE;

	CPRINTS("send 0xAA and 0x55 to FW, reset to boot environment");

	ret = fts_write_reg(reg, FTS_UPGRADE_AA);
	if (ret < 0) {
		CPRINTS("write FC=0xAA fail");
		return ret;
	}
	crec_msleep(FTS_DELAY_UPGRADE_AA);

	ret = fts_write_reg(reg, FTS_UPGRADE_55);
	if (ret < 0) {
		CPRINTS("write FC=0x55 fail");
		return ret;
	}

	crec_msleep(FTS_DELAY_UPGRADE_RESET);
	return 0;
}

/************************************************************************
 * Name: fts_fwupg_check_flash_status
 * Brief: read status from tp
 * Input: flash_status: correct value from tp
 *        retries: read retry times
 *        retries_delay: retry delay
 * Output:
 * Return: return true if flash status check pass, otherwise return false
 ***********************************************************************/
static bool fts_fwupg_check_flash_status(uint16_t flash_status, int retries,
					 int retries_delay)
{
	int ret = 0;
	int i = 0;
	uint8_t cmd = 0;
	uint8_t val[2] = { 0 };
	uint16_t read_status = 0;
	for (i = 0; i < retries; i++) {
		cmd = FTS_CMD_FLASH_STATUS;
		ret = fts_read(&cmd, 1, val, 2);
		read_status = (((uint16_t)val[0]) << 8) + val[1];
		if (flash_status == read_status) {
			return true;
		}
		watchdog_reload();
		crec_msleep(retries_delay);
	}

	CPRINTS("flash status fail,ok:%04x,read:%04x,retries:%d", flash_status,
		read_status, i);
	return false;
}

/************************************************************************
 * Name: fts_fwupg_erase
 * Brief: erase flash area
 * Input: delay - delay after erase
 * Output:
 * Return: return 0 if success, otherwise return error code
 ***********************************************************************/
static int fts_fwupg_erase(uint32_t delay)
{
	int ret = 0;
	uint8_t cmd = 0;
	bool flag = false;

	CPRINTS("**********erase now**********");

	/*send to erase flash*/
	cmd = FTS_CMD_ERASE_APP;
	ret = fts_write(&cmd, 1);
	if (ret < 0) {
		CPRINTS("erase cmd fail");
		return ret;
	}
	watchdog_reload();
	crec_msleep(delay);
	/* read status 0xF0AA: success */
	flag = fts_fwupg_check_flash_status(FTS_CMD_FLASH_STATUS_ERASE_OK,
					    FTS_RETRIES_REASE,
					    FTS_RETRIES_DELAY_REASE);
	if (!flag) {
		CPRINTS("ecc flash status check fail");
		return -1;
	}

	return 0;
}

/************************************************************************
 * Name: fts_ft5z8b_flash_write_buf
 * Brief: write buf data to flash address
 * Input: saddr - start address data write to flash
 *        buf - data buffer
 *        len - data length
 *        delay - delay after write
 * Output:
 * Return: return data ecc of host if success, otherwise return error code
 ***********************************************************************/
static int fts_flash_write_buf(uint32_t saddr, const uint8_t *buf, uint32_t len,
			       uint32_t delay, uint8_t ecc_pre)
{
	int ret = 0;
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t packet_number = 0;
	uint32_t packet_len = 0;
	uint32_t addr = 0;
	uint32_t offset = 0;
	uint32_t remainder = 0;
	uint32_t cmdlen = 0;
	uint8_t packet_buf[FTS_FLASH_PACKET_SIZE + 6] = { 0 };
	int ecc_in_host = 0;
	uint8_t cmd = 0;
	uint8_t val[2] = { 0 };
	uint16_t read_status = 0;
	uint16_t wr_ok = 0;
	uint32_t flash_packet_size = FTS_FLASH_PACKET_SIZE;

	CPRINTS("**********write data to flash**********");
	if (!buf || !len || (len > 256 * 1024)) {
		CPRINTS("buf/len(%d) is invalid", len);
		return -1;
	}

	CPRINTS("data buf start addr=0x%x, len=0x%x", saddr, len);
	packet_number = len / flash_packet_size;
	remainder = len % flash_packet_size;
	if (remainder > 0)
		packet_number++;
	packet_len = flash_packet_size;
	CPRINTS("write data, num:%d remainder:%d", packet_number, remainder);

	for (i = 0; i < packet_number; i++) {
		offset = i * flash_packet_size;
		addr = saddr + offset;

		/* last packet */
		if ((i == (packet_number - 1)) && remainder)
			packet_len = remainder;

		packet_buf[0] = FTS_CMD_WRITE;
		packet_buf[1] = BYTE_OFF_16(addr);
		packet_buf[2] = BYTE_OFF_8(addr);
		packet_buf[3] = BYTE_OFF_0(addr);
		packet_buf[4] = BYTE_OFF_8(packet_len);
		packet_buf[5] = BYTE_OFF_0(packet_len);
		cmdlen = 6;

		for (j = 0; j < packet_len; j++) {
			packet_buf[cmdlen + j] = buf[offset + j];
			ecc_pre ^= packet_buf[cmdlen + j];
		}

		ret = fts_write(packet_buf, packet_len + cmdlen);
		if (ret < 0) {
			CPRINTS("app write fail");
			return ret;
		}
		crec_msleep(delay);

		/* read status */
		wr_ok = 0x1000 + addr / packet_len;
		for (j = 0; j < 100; j++) {
			cmd = FTS_CMD_FLASH_STATUS;
			ret = fts_read(&cmd, 1, val, 2);
			read_status = (((uint16_t)val[0]) << 8) + val[1];
			/*  FTS_INFO("%x %x", wr_ok, read_status); */
			if (wr_ok == read_status) {
				break;
			}
			crec_msleep(1);
		}
		watchdog_reload();
	}

	ecc_in_host = (int)ecc_pre;
	return ecc_in_host;
}

/************************************************************************
 * Name: fts_fwupg_ecc_cal
 * Brief: calculate and get ecc from tp
 * Input: saddr - start address need calculate ecc
 *        len - length need calculate ecc
 * Output:
 * Return: return data ecc of tp if success, otherwise return error code
 ***********************************************************************/
static int fts_fwupg_ecc_cal(uint32_t saddr, uint32_t len)
{
	int ret = 0;
	uint32_t cmdlen = 7;
	uint8_t wbuf[7] = { 0 };
	uint8_t val[2] = { 0 };
	int ecc = 0;
	uint32_t packet_num = 0;
	uint32_t packet_len = 0;
	uint32_t remainder = 0;
	uint32_t addr = 0;
	uint32_t offset = 0;
	bool bflag = false;

	/* check sum init */
	wbuf[0] = FTS_CMD_ECC_INIT;
	ret = fts_write(wbuf, 1);
	if (ret < 0) {
		CPRINTS("ecc init cmd write fail");
		return ret;
	}
	packet_num = 1;
	remainder = 0;
	packet_len = len;

	CPRINTS("ecc calc num:%d, remainder:%d", packet_num, remainder);

	/* send command to start checksum */
	wbuf[0] = FTS_CMD_ECC_CAL;
	offset = 0;
	addr = saddr + offset;
	wbuf[1] = BYTE_OFF_16(addr);
	wbuf[2] = BYTE_OFF_8(addr);
	wbuf[3] = BYTE_OFF_0(addr);

	wbuf[4] = BYTE_OFF_16(packet_len);
	wbuf[5] = BYTE_OFF_8(packet_len);
	wbuf[6] = BYTE_OFF_0(packet_len);
	cmdlen = 7;

	CPRINTS("ecc calc startaddr:0x%04x, len:%d", addr, packet_len);
	ret = fts_write(wbuf, cmdlen);
	if (ret < 0) {
		CPRINTS("ecc calc cmd write fail");
		return ret;
	}
	crec_msleep(packet_len / 256);

	/* read status if check sum is finished */
	bflag = fts_fwupg_check_flash_status(FTS_CMD_FLASH_STATUS_ECC_OK,
					     FTS_RETRIES_ECC_CAL,
					     FTS_RETRIES_DELAY_ECC_CAL);
	if (!bflag) {
		CPRINTS("ecc flash status read fail");
		return -1;
	}

	/* read out check sum */
	wbuf[0] = FTS_CMD_ECC_READ;
	ret = fts_read(wbuf, 1, val, 1);
	if (ret < 0) {
		CPRINTS("ecc read cmd write fail");
		return ret;
	}
	ecc = (int)val[0];

	return ecc;
}

static int fts_fwupg_reset_in_boot(void)
{
	int ret = 0;
	uint8_t cmd = 0x07;

	CPRINTS("reset in boot environment");
	ret = fts_write(&cmd, 1);
	if (ret < 0) {
		CPRINTS("pram/rom/bootloader reset cmd write fail");
		return ret;
	}
	board_touchpad_reset();

	crec_msleep(FTS_DELAY_UPGRADE_RESET);
	return 0;
}

static int fts_prepare_for_update(void)
{
	int ret = 0;
	uint8_t val = 0;

	fts_read_reg(FTS_REG_CHIP_ID_H, &val);
	if (val == FTS_CHIP_ID_H) {
		ret = fts_enter_upgrade_mode();
		if (ret < 0) {
			CPRINTS("fts_enter_upgrade_mode fail");
			return -1;
		}
	}
	CPRINTS("fts_enter_upgrade_mode success");

	fts_hid2std(0);

	return EC_SUCCESS;
}

int touchpad_update_write(int offset, int size, const uint8_t *data)
{
	uint32_t start_addr = 0;
	uint8_t cmd[4] = { 0 };
	uint32_t delay = 0;
	static int ecc_in_host = 0;
	int ecc_in_tp = 0;
	int size_fect = CONFIG_TOUCHPAD_VIRTUAL_SIZE;
	int ret = 0;

	CPRINTS("%s %08x %d", __func__, offset, size);
	if (offset == 0) {
		gpio_disable_interrupt(GPIO_TOUCHPAD_INT);
		CPRINTS("%s: prepare fw update.", __func__);
		ret = fts_prepare_for_update();
		if (ret < 0)
			return ret;

		cmd[0] = FTS_CMD_DATA_LEN;
		cmd[1] = BYTE_OFF_16(size_fect);
		cmd[2] = BYTE_OFF_8(size_fect);
		cmd[3] = BYTE_OFF_0(size_fect);
		ret = fts_write(cmd, FTS_CMD_DATA_LEN_LEN);
		if (ret < 0) {
			CPRINTS("data len cmd write fail");
			goto fw_reset;
		}

		cmd[0] = 0x09;
		cmd[1] = 0x0B;
		ret = fts_write(cmd, 2);
		if (ret < 0) {
			CPRINTS("upgrade mode(09) cmd write fail");
			goto fw_reset;
		}
		delay = FTS_DELAY_ERASE_PAGE * (size_fect / FTS_SIZE_PAGE);
		ret = fts_fwupg_erase(delay);
		if (ret < 0) {
			CPRINTS("erase cmd write fail");
			goto fw_reset;
		}
		start_addr = 0;
	}
	start_addr = offset;
	/* write app */
	delay = 1;
	ecc_in_host =
		fts_flash_write_buf(start_addr, data, size, delay, ecc_in_host);
	if (ecc_in_host < 0) {
		CPRINTS("flash write fail");
		goto fw_reset;
	}
	if (offset + size == CONFIG_TOUCHPAD_VIRTUAL_SIZE) {
		/* ecc */
		ecc_in_tp = fts_fwupg_ecc_cal(0, CONFIG_TOUCHPAD_VIRTUAL_SIZE);
		if (ecc_in_tp < 0) {
			CPRINTS("ecc read fail");
			goto fw_reset;
		}

		CPRINTS("ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
		if (ecc_in_tp != ecc_in_host) {
			CPRINTS("ecc check fail");
			goto fw_reset;
		}

		CPRINTS("upgrade success, reset to normal boot");
		ret = fts_fwupg_reset_in_boot();
		if (ret < 0) {
			CPRINTS("reset to normal boot fail");
		}
		ecc_in_host = 0;
		crec_msleep(200);
		gpio_enable_interrupt(GPIO_TOUCHPAD_INT);
	}
	return EC_SUCCESS;

fw_reset:
	CPRINTS("upgrade fail, reset to normal boot");
	ret = fts_fwupg_reset_in_boot();
	if (ret < 0) {
		CPRINTS("reset to normal boot fail");
	}
	gpio_enable_interrupt(GPIO_TOUCHPAD_INT);
	return EC_ERROR_INVAL;
}

int touchpad_debug(const uint8_t *param, unsigned int param_size,
		   uint8_t **data, unsigned int *data_size)
{
	return EC_SUCCESS;
}

#endif

/*
 * Try to read touchpad report up to 3 times, reset the touchpad if we still
 * fail.
 */
void fts_tp_read_report_retry(void)
{
	int ret;
	int retry = 3;

	while (retry--) {
		ret = fts_tp_read_report();

		if (ret == 0)
			return;

		/* Try again */
		crec_msleep(1);
	}

	/* Failed to read data, reset the touchpad. */
	CPRINTF("Resetting TP.");
	board_touchpad_reset();
	crec_msleep(200);
	fts_tp_init();
}

void touchpad_interrupt(enum gpio_signal signal)
{
	irq_ts = __hw_clock_source_read();

	task_wake(TASK_ID_TOUCHPAD);
}

/* Make a decision on touchpad power, based on USB and tablet mode status. */
static void touchpad_power_control(void)
{
	static int enabled = 1;
	int enable = 1;

#ifdef CONFIG_USB_SUSPEND
	enable = enable &&
		 (!usb_is_suspended() || usb_is_remote_wakeup_enabled());
#endif

#ifdef CONFIG_TABLET_MODE
	enable = enable && !tablet_get_mode();
#endif

	if (enabled == enable)
		return;

	fts_tp_set_power(enable);

	enabled = enable;
}

void touchpad_task(void *u)
{
	uint32_t event;

	fts_tp_init();
	touchpad_power_control();

	while (1) {
		event = task_wait_event(-1);

		if (event & TASK_EVENT_WAKE)
			fts_tp_read_report_retry();

		if (event & TASK_EVENT_POWER)
			touchpad_power_control();
	}
}

/*
 * When USB PM status changes, or tablet mode changes, call in the main task to
 * decide whether to turn touchpad on or off.
 */
#if defined(CONFIG_USB_SUSPEND) || defined(CONFIG_TABLET_MODE)
static void touchpad_power_change(void)
{
	task_set_event(TASK_ID_TOUCHPAD, TASK_EVENT_POWER);
}
#endif
#ifdef CONFIG_USB_SUSPEND
DECLARE_HOOK(HOOK_USB_PM_CHANGE, touchpad_power_change, HOOK_PRIO_DEFAULT);
#endif
#ifdef CONFIG_TABLET_MODE
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, touchpad_power_change, HOOK_PRIO_DEFAULT);
#endif
