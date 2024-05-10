/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "cps8200_bootloader.h"
#include "crc.h"
#include "gpio.h"
#include "i2c.h"
#include "peripheral_charger.h"
#include "printf.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Print additional data */
#define CPS8100_DEBUG

#define CPUTS(outstr) cputs(CC_PCHG, outstr)
#define CPRINTS(fmt, args...) cprints(CC_PCHG, "CPS8100: " fmt, ##args)
#define CPRINTFP(fmt, args...) cprintf(CC_PCHG, "CPS8100: " fmt, ##args)
#define CPRINTF(fmt, args...) cprintf(CC_PCHG, fmt, ##args)

/*
 * Configuration
 */
#define CPS8100_I2C_ADDR_H 0x31
#define CPS8100_I2C_ADDR_L 0x30
#define CPS8200_I2C_ADDR 0x30

/* High address registers (commands?) */
#define CPS8100_REGH_PASSWORD 0xf500
#define CPS8100_REGH_ACCESS_MODE 0xf505
#define CPS8100_REGH_ADDRESS 0xf503

#define CPS8100_ACCESS_MODE_8 0x00
#define CPS8100_ACCESS_MODE_16 0x01
#define CPS8100_ACCESS_MODE_32 0x02
#define CPS8100_PASSWORD 0x19e5
#define CPS8100_CHIPID 0x8100
#define CPS8200_CHIPID 0x8200

#define CPS8200_I2C_ENABLE 0x0000000E
#define CPS8200_PASSWORD 0x00001250

/* Registers */
#define CPS8100_REG_IC_INFO 0x20000000
#define CPS8100_REG_FW_INFO 0x20000004
#define CPS8100_REG_FUNC_EN 0x2000003c
#define CPS8100_REG_ALERT_INFO 0x20000158
#define CPS8100_REG_INT_ENABLE 0x20000160
#define CPS8100_REG_INT_FLAG 0x20000164

#define CPS8200_REG_I2C_ENABLE 0xFFFFFF00
#define CPS8200_REG_PASSWORD 0x400140FC

/* Firmware update */
#define CPS8200_ADDR_SRAM 0x20000000
#define CPS8200_ADDR_BUFFER0 0x20002800
#define CPS8200_ADDR_BUFFER1 0x20003000
#define CPS8200_ADDR_CMD 0x200038F8
#define CPS8200_ADDR_CMD_STATUS 0x200038FC
#define CPS8200_ADDR_BUF_SIZE 0x200038F4
#define CFG_BUFF_SIZE 128
#define CPS8200_CMD_TIMEOUT (3 * SECOND)
/* CMD and CMD status. Lower 4 bits are for command ID */
#define CMD_PGM_BUFFER0 (0x1 << 4)
#define CMD_PGM_BUFFER1 (0x2 << 4)
#define CMD_PGM_WR_FLAG (0x8 << 4)
#define CMD_CACL_CRC_APP (0x9 << 4)
#define CMD_CACL_CRC_BOOT (0xB << 4)
#define CMD_STATUS_RUNNING (0x1 << 4)
#define CMD_STATUS_PASS (0x2 << 4)
#define CMD_STATUS_FAIL (0x3 << 4)
#define CMD_STATUS_ILLEGAL (0x4 << 4)

#define CPS8100_STATUS_PROFILE(r) (((r) & GENMASK(5, 4)) >> 4)
#define CPS8100_STATUS_CHARGE(r) ((r) & BIT(6))
#define CPS8100_STATUS_DEVICE(r) ((r) & BIT(7))
#define CPS8100_STATUS_BATTERY(r) (((r) & GENMASK(15, 8)) >> 8)
#define CPS8100_IRQ_TYPE(r) (((r) & GENMASK(23, 20)) >> 20)
#define CPS8200_CMD_STATUS(flag) ((flag) & GENMASK(7, 4))
#define CPS8X00_GET_FW_VER(value) ((value) & GENMASK(7, 0))
#define CPS8X00_BAD_FW_VERSION 0xff
#define CPS8200_CMD_MASK GENMASK(7, 4)
#define CPS8200_CMD_ID_MASK GENMASK(3, 0)

/* Status flags in ALERT_INFO register */
#define CPS8100_STATUS_FOD BIT(0)
#define CPS8100_STATUS_OCP BIT(1)
#define CPS8100_STATUS_OVP BIT(2)
#define CPS8100_STATUS_OTP BIT(3)
#define CPS8100_STATUS_UVP BIT(16)

/* Buffer size for i2c read & write */
#define CPS8100_MESSAGE_BUFFER_SIZE 0x20

/*
 * CPS8100 needs 100~120ms delay and CPS8200 needs 40~50ms delay between
 * reset and the first access to I2C register.
 */
#define CPS8200_POWER_ON_DELAY_MS 50
#define CPS8100_POWER_ON_DELAY_MS 120

static uint32_t chip_id;
static const int short_sleep_ms = 2;

/* TODO: Check datasheet how to wake up and how long it takes to wake up. */
static const int cps8100_wake_up_delay_ms = 10;

/* TODO: This should belong to PCHG context. Driver should be state-less. */
struct cps8100_state {
	struct mutex mtx;
	uint32_t reg;
};

/* Note: Assume there is only one port. */
static struct cps8100_state cps8100_state;

struct cps8100_msg {
	/* Data address */
	uint8_t addr[2];
	/* Data. Can be used for read as well. */
	uint8_t data[2];
} __packed;

static int (*cps8x00_read32)(int port, uint32_t reg, uint32_t *val);

/* This driver isn't compatible with big endian. */
BUILD_ASSERT(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

static const char *const cps8100_func_names[] = {
	[0] = "DPL",	 [1] = "OPP",	[2] = "OTP",  [3] = "OVPK",
	[4] = "OCP",	 [5] = "UVP",	[6] = "OVP",  [7] = "FOD",
	[8] = "SAMSUNG", [9] = "APPLE", [10] = "EPP", [11] = "HUAWEI",
	[12] = "CPS",
};

enum cps8100_irq_type {
	CPS8100_IRQ_TYPE_FOD = 0,
	CPS8100_IRQ_TYPE_OCP = 1,
	CPS8100_IRQ_TYPE_OVP = 2,
	CPS8100_IRQ_TYPE_OTP = 3,
	CPS8100_IRQ_TYPE_PROFILE = 4,
	CPS8100_IRQ_TYPE_CHARGE = 5,
	CPS8100_IRQ_TYPE_DEVICE = 6,
	CPS8100_IRQ_TYPE_BATTERY = 7,
	CPS8100_IRQ_TYPE_UVP = 8,
	CPS8100_IRQ_TYPE_RESET = 9,
	CPS8100_IRQ_TYPE_COUNT
};

static const char *const cps8100_irq_type_names[] = {
	[CPS8100_IRQ_TYPE_FOD] = "FOD",
	[CPS8100_IRQ_TYPE_OCP] = "OCP",
	[CPS8100_IRQ_TYPE_OVP] = "OVP",
	[CPS8100_IRQ_TYPE_OTP] = "OTP",
	[CPS8100_IRQ_TYPE_PROFILE] = "PROFILE",
	[CPS8100_IRQ_TYPE_CHARGE] = "CHARGE",
	[CPS8100_IRQ_TYPE_DEVICE] = "DEVICE",
	[CPS8100_IRQ_TYPE_BATTERY] = "BATTERY",
	[CPS8100_IRQ_TYPE_UVP] = "UVP",
	[CPS8100_IRQ_TYPE_RESET] = "RESET",
};

static const char *const cps8100_profile_names[] = {
	[0] = "NONE",
	[1] = "BPP",
	[2] = "EPP",
	[3] = "PRIVATE",
};

static void cps8100_print_func_names(const char *preamble, uint32_t reg)
{
	int i;

	CPRINTFP("%s", preamble);

	for (i = 0; i < ARRAY_SIZE(cps8100_func_names); i++) {
		if (reg & BIT(i)) {
			reg &= ~BIT(i);
			CPRINTF("%s,", cps8100_func_names[i]);
		}
	}

	if (reg)
		CPRINTF("UNKNOWN(0x%x)", reg);

	CPUTS("\n");
}

static void cps8100_print_status_flag_names(const char *preamble, uint32_t reg)
{
	CPRINTFP("%s", preamble);

	if (reg & CPS8100_STATUS_FOD)
		CPRINTF("FOD,");
	if (reg & CPS8100_STATUS_OCP)
		CPRINTF("OCP,");
	if (reg & CPS8100_STATUS_OVP)
		CPRINTF("OVP,");
	if (reg & CPS8100_STATUS_OTP)
		CPRINTF("OTP,");
	if (reg & CPS8100_STATUS_UVP)
		CPRINTF("UVP,");

	CPUTS("\n");
}

static void cps8100_print_irq_type_names(const char *preamble, uint32_t reg)
{
	int type;

	CPRINTFP("%s", preamble);

	type = CPS8100_IRQ_TYPE(reg);
	if (type >= CPS8100_IRQ_TYPE_COUNT)
		CPRINTF("UNKNOWN(%d), ", type);
	else
		CPRINTF("%s", cps8100_irq_type_names[type]);
	CPUTS("\n");
}

static void cps8100_status_update(struct pchg *ctx, uint32_t reg)
{
	mutex_lock(&cps8100_state.mtx);
	cps8100_state.reg = reg;
	mutex_unlock(&cps8100_state.mtx);
}

static int cps8100_i2c_write(int port, int addr, const uint8_t *buf, size_t len)
{
	int rv;

	/* Assumes a write is always 2 bytes. */
	rv = i2c_xfer(port, addr, buf, len, NULL, 0);

	if (rv) {
		crec_msleep(cps8100_wake_up_delay_ms);
		rv = i2c_xfer(port, addr, buf, len, NULL, 0);
	}
	if (rv)
		CPRINTS("Failed to write: %d", rv);

	return rv;
}

static int cps8100_set_unlock(int port)
{
	const uint8_t cps8100_unlock_cmd[] = {
		/* Password register address */
		(CPS8100_REGH_PASSWORD >> 8) & 0xff,
		(CPS8100_REGH_PASSWORD >> 0) & 0xff,
		/* Password */
		(CPS8100_PASSWORD >> 0) & 0xff,
		(CPS8100_PASSWORD >> 8) & 0xff,
	};

	return cps8100_i2c_write(port, CPS8100_I2C_ADDR_H, cps8100_unlock_cmd,
				 4);
}

static int cps8200_set_unlock(int port)
{
	const uint8_t cps8200_unlock_cmd[] = {
		/* Password register addr */
		(CPS8200_REG_PASSWORD >> 24) & 0xff,
		(CPS8200_REG_PASSWORD >> 16) & 0xff,
		(CPS8200_REG_PASSWORD >> 8) & 0xff,
		(CPS8200_REG_PASSWORD >> 0) & 0xff,
		/* Password */
		(CPS8200_PASSWORD >> 0) & 0xff,
		(CPS8200_PASSWORD >> 8) & 0xff,
		(CPS8200_PASSWORD >> 16) & 0xff,
		(CPS8200_PASSWORD >> 24) & 0xff,
	};

	return cps8100_i2c_write(port, CPS8200_I2C_ADDR, cps8200_unlock_cmd, 8);
}

static int cps8200_i2c_enable(int port)
{
	const uint8_t cps8200_i2c_enable_cmd[] = {
		/* addr */
		(CPS8200_REG_I2C_ENABLE >> 24) & 0xff,
		(CPS8200_REG_I2C_ENABLE >> 16) & 0xff,
		(CPS8200_REG_I2C_ENABLE >> 8) & 0xff,
		(CPS8200_REG_I2C_ENABLE >> 0) & 0xff,
		/* data */
		(CPS8200_I2C_ENABLE >> 0) & 0xff,
		(CPS8200_I2C_ENABLE >> 8) & 0xff,
		(CPS8200_I2C_ENABLE >> 16) & 0xff,
		(CPS8200_I2C_ENABLE >> 24) & 0xff,
	};

	return cps8100_i2c_write(port, CPS8200_I2C_ADDR, cps8200_i2c_enable_cmd,
				 8);
}

static int cps8100_set_write_mode(int port, uint8_t mode)
{
	uint8_t buf[4];

	buf[0] = 0xf5;
	buf[1] = 0x05;
	buf[2] = mode;
	buf[3] = 0x00;

	return cps8100_i2c_write(port, CPS8100_I2C_ADDR_H, buf, 4);
}

static int cps8100_set_high_address(int port, uint32_t addr)
{
	uint8_t buf[4];

	buf[0] = 0xf5;
	buf[1] = 0x03;
	buf[2] = (addr >> 16) & 0xff;
	buf[3] = (addr >> 24) & 0xff;

	return cps8100_i2c_write(port, CPS8100_I2C_ADDR_H, buf, 4);
}

static int cps8100_read32(int port, uint32_t reg, uint32_t *val)
{
	uint8_t buf[CPS8100_MESSAGE_BUFFER_SIZE];

	if (cps8100_set_high_address(port, reg))
		return EC_ERROR_UNKNOWN;

	/* Set low 16 bits of register address and read a byte. */
	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;

	return i2c_xfer(port, CPS8100_I2C_ADDR_L, buf, 2, (void *)val,
			sizeof(*val));
}

static int cps8200_write32(int port, uint32_t reg, uint32_t val)
{
	uint8_t buf[8];

	buf[0] = (reg >> 24) & 0xff;
	buf[1] = (reg >> 16) & 0xff;
	buf[2] = (reg >> 8) & 0xff;
	buf[3] = (reg >> 0) & 0xff;

	buf[4] = (val >> 0) & 0xff;
	buf[5] = (val >> 8) & 0xff;
	buf[6] = (val >> 16) & 0xff;
	buf[7] = (val >> 24) & 0xff;

	return cps8100_i2c_write(port, CPS8200_I2C_ADDR, buf, sizeof(buf));
}

static int cps8200_read32(int port, uint32_t reg, uint32_t *val)
{
	uint8_t buf[4];

	buf[0] = (reg >> 24) & 0xff;
	buf[1] = (reg >> 16) & 0xff;
	buf[2] = (reg >> 8) & 0xff;
	buf[3] = (reg >> 0) & 0xff;

	return i2c_xfer(port, CPS8200_I2C_ADDR, buf, sizeof(buf), (void *)val,
			sizeof(*val));
}

static int cps8200_write_mem(int port, uint32_t addr, uint8_t *data, size_t len)
{
	int rv;
	uint8_t buf[4];

	buf[0] = (addr >> 24) & 0xff;
	buf[1] = (addr >> 16) & 0xff;
	buf[2] = (addr >> 8) & 0xff;
	buf[3] = (addr >> 0) & 0xff;

	i2c_lock(port, 1);
	/* Addr */
	rv = i2c_xfer_unlocked(port, CPS8200_I2C_ADDR, buf, sizeof(buf), NULL,
			       0, I2C_XFER_START);
	if (rv) {
		i2c_lock(port, 0);
		return rv;
	}
	/* Data */
	rv = i2c_xfer_unlocked(port, CPS8200_I2C_ADDR, data, len, NULL, 0,
			       I2C_XFER_STOP);
	i2c_lock(port, 0);

	return rv;
}

static int cps8100_unlock(int port)
{
	int rv;

	rv = cps8100_set_unlock(port);
	return rv ? rv : cps8100_set_write_mode(port, CPS8100_ACCESS_MODE_32);
}

static int cps8200_unlock(int port)
{
	int rv;

	rv = cps8200_i2c_enable(port);
	return rv ? rv : cps8200_set_unlock(port);
}

/*
 * Send command to CPS8200 by writing command to CPS8200_ADDR_CMD register.
 * command = cmd (higher 4 bits) + id (lower 4 bits), id needs to be increased
 * every time.
 */
static uint8_t cps8200_send_cmd(struct pchg *ctx, uint8_t cmd, uint8_t *id)
{
	uint8_t port = ctx->cfg->i2c_port;
	struct cps8x00_update *upd = &(ctx->update.driver_data.cps8200_update);

	*id = upd->cmd_id;
	cmd &= CPS8200_CMD_MASK;
	cmd |= *id;
	upd->cmd_id++;
	upd->cmd_id &= CPS8200_CMD_ID_MASK;

	return cps8200_write32(port, CPS8200_ADDR_CMD, (uint32_t)cmd);
}

/*
 * Read the response of command by reading CPS8200_ADDR_CMD_STATUS register.
 * Return EC_SUCCESS if the status is CMD_STATUS_PASS and the id matches to
 * the id which is expected.
 */
static int cps8200_wait_cmd_done(int port, uint8_t id)
{
	int rv;
	uint32_t u32;
	timestamp_t deadline;

	deadline.val = get_time().val + CPS8200_CMD_TIMEOUT;
	while (1) {
		crec_msleep(10);
		rv = cps8200_read32(port, CPS8200_ADDR_CMD_STATUS, &u32);
		if (rv)
			return EC_ERROR_UNKNOWN;

		if ((CMD_STATUS_PASS | id) == (u32 & 0x00ff))
			break;
		if (CPS8200_CMD_STATUS(u32) == CMD_STATUS_FAIL ||
		    CPS8200_CMD_STATUS(u32) == CMD_STATUS_ILLEGAL) {
			CPRINTS("Command failed or illegal: %02x",
				CPS8200_CMD_STATUS(u32));
			return EC_ERROR_UNKNOWN;
		}

		rv = timestamp_expired(deadline, NULL);
		if (rv) {
			CPRINTS("Command timeout!");
			return EC_ERROR_TIMEOUT;
		}
	}
	return EC_SUCCESS;
}

static int cps8x00_read_firmware_ver(struct pchg *ctx)
{
	uint32_t u32;
	int port = ctx->cfg->i2c_port;
	int rv;

	rv = cps8x00_read32(port, CPS8100_REG_FW_INFO, &u32);
	if (!rv) {
		ctx->fw_version = CPS8X00_GET_FW_VER(u32);
	} else {
		ctx->fw_version = CPS8X00_BAD_FW_VERSION;
		CPRINTS("Failed to read FW info: %d", rv);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static int cps8x00_reset(struct pchg *ctx)
{
	gpio_set_level(GPIO_QI_RESET_L, 0);
	cps8100_status_update(ctx, 0);
	udelay(15);
	gpio_set_level(GPIO_QI_RESET_L, 1);

	return EC_SUCCESS;
}

static int cps8x00_init(struct pchg *ctx)
{
	int port = ctx->cfg->i2c_port;

	/* Enable I2C, unlock and set mode */;
	if (chip_id == CPS8100_CHIPID)
		return cps8100_unlock(port);
	else if (chip_id == CPS8200_CHIPID)
		return cps8200_unlock(port);
	else
		return EC_ERROR_UNKNOWN;
}

static int cps8x00_enable(struct pchg *ctx, bool enable)
{
	return EC_SUCCESS;
}

static int cps8100_get_alert_info(struct pchg *ctx, uint32_t *reg)
{
	int rv;

	rv = cps8x00_read32(ctx->cfg->i2c_port, CPS8100_REG_ALERT_INFO, reg);
	if (rv) {
		CPRINTS("Failed to get alert info (%d)", rv);
		return rv;
	}

	return EC_SUCCESS;
}

static int cps8x00_get_chip_info(struct pchg *ctx)
{
	uint32_t u32;
	int port = ctx->cfg->i2c_port;
	int rv = EC_ERROR_UNKNOWN;

	if (chip_id == CPS8100_CHIPID) {
		/*
		 * already probed but unlock again in case it's turned
		 * off.
		 */
		crec_msleep(CPS8100_POWER_ON_DELAY_MS);
		return cps8100_unlock(port);
	} else if (chip_id == CPS8200_CHIPID) {
		crec_msleep(CPS8200_POWER_ON_DELAY_MS);
		return cps8200_unlock(port);
	}

	/* not probed yet, need to unlock blindly first. */
	crec_msleep(MAX(CPS8100_POWER_ON_DELAY_MS, CPS8200_POWER_ON_DELAY_MS));
	if (!cps8100_unlock(port))
		rv = cps8100_read32(port, CPS8100_REG_IC_INFO, &u32);
	else if (!cps8200_unlock(port))
		rv = cps8200_read32(port, CPS8100_REG_IC_INFO, &u32);

	if (rv) {
		CPRINTS("Failed to read IC info!");
		return rv;
	}

	/* Probe */
	CPRINTS("IC=0x%08x", u32);
	if ((u32 & 0xffff) == CPS8100_CHIPID) {
		cps8x00_read32 = cps8100_read32;
		chip_id = CPS8100_CHIPID;
	} else if ((u32 & 0xffff) == CPS8200_CHIPID) {
		cps8x00_read32 = cps8200_read32;
		chip_id = CPS8200_CHIPID;
	} else {
		CPRINTS("Unknown chip!");
		return EC_ERROR_UNKNOWN;
	}

	rv = cps8x00_read_firmware_ver(ctx);
	if (!rv)
		CPRINTS("FW=0x%02x", ctx->fw_version);

	return EC_SUCCESS;
}

static void cps8100_print_alert_info(uint32_t reg)
{
	cps8100_print_irq_type_names("IRQ_TYPE: ", reg);
	cps8100_print_status_flag_names("ERRORS: ", reg);

	CPRINTFP("Profile: %s\n",
		 cps8100_profile_names[CPS8100_STATUS_PROFILE(reg)]);
	CPRINTFP("%sCharging\n", CPS8100_STATUS_CHARGE(reg) ? "" : "Not ");
	CPRINTFP("Device %sPresent\n",
		 CPS8100_STATUS_DEVICE(reg) ? "" : "Not ");
	CPRINTFP("Battery: %d%%\n", CPS8100_STATUS_BATTERY(reg));
}

static int cps8x00_get_event(struct pchg *ctx)
{
	uint32_t r1, r2;
	int rv;

	r1 = cps8100_state.reg;
	rv = cps8100_get_alert_info(ctx, &r2);
	if (rv)
		return rv;

	if (IS_ENABLED(CPS8100_DEBUG))
		cps8100_print_alert_info(r2);

	/* Check status change in the order of priority. */
	if (CPS8100_IRQ_TYPE(r2) == CPS8100_IRQ_TYPE_RESET) {
		ctx->event = PCHG_EVENT_RESET;
	} else if (!CPS8100_STATUS_DEVICE(r1) && CPS8100_STATUS_DEVICE(r2)) {
		ctx->event = PCHG_EVENT_DEVICE_CONNECTED;
	} else if (CPS8100_STATUS_DEVICE(r1) && !CPS8100_STATUS_DEVICE(r2)) {
		ctx->event = PCHG_EVENT_DEVICE_LOST;
	} else if (CPS8100_STATUS_PROFILE(r1) != CPS8100_STATUS_PROFILE(r2)) {
		ctx->event = PCHG_EVENT_CHARGE_UPDATE;
		ctx->battery_percent = CPS8100_STATUS_BATTERY(r2);
	} else if (!CPS8100_STATUS_CHARGE(r1) && CPS8100_STATUS_CHARGE(r2)) {
		ctx->event = PCHG_EVENT_CHARGE_STARTED;
	} else if (CPS8100_STATUS_CHARGE(r1) && !CPS8100_STATUS_CHARGE(r2)) {
		ctx->event = PCHG_EVENT_CHARGE_STOPPED;
	} else if (CPS8100_STATUS_BATTERY(r1) != CPS8100_STATUS_BATTERY(r2)) {
		ctx->event = PCHG_EVENT_CHARGE_UPDATE;
		ctx->battery_percent = CPS8100_STATUS_BATTERY(r2);
	} else if (ctx->state == PCHG_STATE_RESET) {
		ctx->event = PCHG_EVENT_RESET;
	} else {
		ctx->event = PCHG_EVENT_NONE;
	}

	cps8100_status_update(ctx, r2);

	return EC_SUCCESS;
}

static int cps8x00_get_soc(struct pchg *ctx)
{
	ctx->battery_percent = CPS8100_STATUS_BATTERY(cps8100_state.reg);

	return EC_SUCCESS;
}

/*
 * This function does the preparation for firmware update, the steps are:
 * 1. Enable i2c and unlock.
 * 2. Reset MCU, reset watchdog, disable DCDC and reset MCU clock.
 * 3. Program bootloader to SRAM.
 * 4. Run bootloader.
 * 5. Check CRC of bootloader.
 * 6. Ready for firmware download. Configure buffer size for firmware download.
 */
static int cps8200_update_open(struct pchg *ctx)
{
	uint32_t u32;
	uint8_t id;
	int port = ctx->cfg->i2c_port;
	int rv;
	struct cps8x00_update *upd = &(ctx->update.driver_data.cps8200_update);

	upd->cmd_id = 0;
	upd->crc = 0;
	upd->firmware_len = 0;

	/* enable i2c and unlock */
	rv = cps8200_unlock(port);
	if (rv)
		return rv;

	/*
	 * The value and address of the registers are from the sample code
	 * and programming guide provided by the vendor in
	 * https://issuetracker.google.com/issues/195708351#comment32
	 */

	/* Reset MCU and halt */
	rv = cps8200_write32(port, 0x40014028, 0x00010000);
	if (rv)
		return rv;
	crec_msleep(50);

	/* Reset watchdog */
	rv = cps8200_write32(port, 0x40008400, 0x1ACCE551);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);
	rv = cps8200_write32(port, 0x40008008, 0x0);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* Disable DCDC module */
	rv = cps8200_write32(port, 0x4000F0A4, 0x0);
	if (rv)
		return rv;
	crec_msleep(50);

	/* Reset MCU clock */
	rv = cps8200_write32(port, 0x40014020, 0x0);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);
	rv = cps8200_write32(port, 0x40014024, 0x0);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);
	rv = cps8200_write32(port, 0x400140A8, 0x0);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* Program bootloader to SRAM */
	CPRINTS("Loading bootloader hex!");
	if (cps8200_write_mem(port, CPS8200_ADDR_SRAM, (uint8_t *)&boot_hex,
			      boot_hex_len * 4)) {
		CPRINTS("Failed to write bootloader!");
		return EC_ERROR_UNKNOWN;
	}
	crec_msleep(short_sleep_ms);

	/* disable trim */
	rv = cps8200_write32(port, 0x4001F01C, 0x0);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* enable address remap */
	rv = cps8200_write32(port, 0x4001F030, 0xFFFFFF00);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);
	rv = cps8200_write32(port, 0x4001F034, 0xFFFFFFFF);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);
	rv = cps8200_write32(port, 0x4001F038, 0xFFFFFFFF);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);
	rv = cps8200_write32(port, 0x4001F03C, 0xFFFFFFFF);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* disable mcu halt, run bootloader */
	rv = cps8200_write32(port, 0x40014028, 0x101);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* enable i2c and unlock */
	rv = cps8200_i2c_enable(port);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* write bootloader length */
	rv = cps8200_write32(port, CPS8200_ADDR_BUFFER0, boot_hex_len * 4);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* calculate CRC of bootloader */
	rv = cps8200_send_cmd(ctx, CMD_CACL_CRC_BOOT, &id);
	if (rv)
		return rv;

	/* check command status */
	rv = cps8200_wait_cmd_done(port, id);
	if (rv)
		return rv;
	crec_msleep(100);

	/* check CRC */
	rv = cps8200_read32(port, CPS8200_ADDR_BUFFER0, &u32);
	if (rv)
		return rv;
	upd->crc = cros_crc16((uint8_t *)boot_hex, boot_hex_len * 4, 0);
	if (upd->crc != (u32 & 0x0000ffff)) {
		CPRINTS("crc = %04x, expect %04x", u32, upd->crc);
		CPRINTS("CRC of bootloader is wroing!");
		return EC_ERROR_UNKNOWN;
	}
	CPRINTS("Successfully load bootloader!");

	upd->crc = 0x0000;
	/* Prepare to download firmware and program flash, change buffer size */
	rv = cps8200_write32(port, CPS8200_ADDR_BUF_SIZE,
			     ctx->cfg->block_size / 4);
	if (rv) {
		CPRINTS("Failed to change buffer size (%d)", rv);
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/*
 * This function writes the firmware block to the chip and returns only after
 * the write is complete. The steps are:
 * 1. Write firmware block to the chip buffer.
 * 2. Send command to the chip.
 * 3. The chip program the flash.
 * 4. Calculate and update CRC.
 */
static int cps8200_update_write(struct pchg *ctx)
{
	int port = ctx->cfg->i2c_port;
	int rv;
	uint8_t id;
	uint8_t *buf = ctx->update.data;
	struct cps8x00_update *upd = &(ctx->update.driver_data.cps8200_update);

	/* Write data to buffer */
	if (cps8200_write_mem(port, CPS8200_ADDR_BUFFER0, buf,
			      ctx->update.size))
		return EC_ERROR_UNKNOWN;
	crec_msleep(short_sleep_ms);

	/* Write buffer to flash */
	rv = cps8200_send_cmd(ctx, CMD_PGM_BUFFER0, &id);
	if (rv)
		return rv;

	/* Check the program result */
	rv = cps8200_wait_cmd_done(port, id);
	if (rv) {
		CPRINTS("Failed to write flash : %d", rv);
		return EC_ERROR_UNKNOWN;
	}

	/* Calculate and update CRC */
	upd->firmware_len += ctx->update.size;
	upd->crc = cros_crc16(ctx->update.data, ctx->update.size, upd->crc);

	return EC_SUCCESS;
}

/*
 * This function checks firmware update result, power on and power off the chip
 * if firmware update is successful. The steps are:
 * 1. Send command to the chip to calculate the firmware CRC.
 * 2. Read the CRC value from the chip and compare.
 * 3. If CRC is correct, power off and power on the chip.
 */
static int cps8200_update_close(struct pchg *ctx)
{
	int port = ctx->cfg->i2c_port;
	int rv;
	uint32_t u32;
	uint8_t id;
	struct cps8x00_update *upd = &(ctx->update.driver_data.cps8200_update);
	uint32_t len = upd->firmware_len;

	/* Write firmware length */
	rv = cps8200_write32(port, CPS8200_ADDR_BUFFER0, len);
	if (rv)
		return rv;
	crec_msleep(short_sleep_ms);

	/* Check firmware CRC */
	CPRINTS("Checking Firmware CRC...");
	rv = cps8200_send_cmd(ctx, CMD_CACL_CRC_APP, &id);
	if (rv)
		return rv;

	rv = cps8200_wait_cmd_done(port, id);
	if (rv) {
		CPRINTS("Command to calculate CRC timeout or failed: %d", rv);
		return EC_ERROR_UNKNOWN;
	}
	crec_msleep(100);

	cps8200_read32(port, CPS8200_ADDR_BUFFER0, &u32);
	if (upd->crc != (u32 & 0x0000ffff)) {
		CPRINTS("crc = %04x, expect %04x", u32, upd->crc);
		CPRINTS("CRC of firmware is wroing!");
		return EC_ERROR_UNKNOWN;
	}
	CPRINTS("Firmware CRC is correct!");
	CPRINTS("Successfully update the firmware");

	rv = cps8200_send_cmd(ctx, CMD_PGM_WR_FLAG, &id);
	if (rv)
		return rv;

	rv = cps8200_wait_cmd_done(port, id);
	if (rv) {
		CPRINTS("Command to program flash failed: %d", rv);
		return EC_ERROR_UNKNOWN;
	}

	/*
	 * Due to the CPS8200 issue
	 * (https://issuetracker.google.com/issues/258093708), we can't simply
	 * reset CPS8200. CPS8200 needs to be powered off to completely reset
	 * its internal modules after completing the firmware update, and then
	 * power on after some delay. We need 50ms delay after power on to
	 * ensure there is enough delay before first I2C command
	 */

	/* power off MCU */
	board_pchg_power_on(PCHG_CTX_TO_PORT(ctx), 0);
	crec_msleep(short_sleep_ms);
	/* power on MCU */
	board_pchg_power_on(PCHG_CTX_TO_PORT(ctx), 1);
	crec_msleep(CPS8200_POWER_ON_DELAY_MS);
	/* Update the information of firmware version */
	cps8200_unlock(port);
	rv = cps8x00_read_firmware_ver(ctx);
	if (!rv)
		CPRINTS("FW=0x%02x", ctx->fw_version);

	return EC_SUCCESS;
}

const struct pchg_drv cps8100_drv = {
	.reset = cps8x00_reset,
	.init = cps8x00_init,
	.enable = cps8x00_enable,
	.get_chip_info = cps8x00_get_chip_info,
	.get_event = cps8x00_get_event,
	.get_soc = cps8x00_get_soc,
};

const struct pchg_drv cps8200_drv = {
	.reset = cps8x00_reset,
	.init = cps8x00_init,
	.enable = cps8x00_enable,
	.get_chip_info = cps8x00_get_chip_info,
	.get_event = cps8x00_get_event,
	.get_soc = cps8x00_get_soc,
	.update_open = cps8200_update_open,
	.update_write = cps8200_update_write,
	.update_close = cps8200_update_close,
};

static void cps8100_dump(struct pchg *ctx)
{
	uint32_t val;
	int rv;

	rv = cps8x00_read32(ctx->cfg->i2c_port, CPS8100_REG_FUNC_EN, &val);
	if (rv == EC_SUCCESS)
		cps8100_print_func_names("FEATURES: ", val);

	rv = cps8100_get_alert_info(ctx, &val);
	if (rv == EC_SUCCESS)
		cps8100_print_alert_info(val);
}

static int cc_cps8100(int argc, const char **argv)
{
	struct pchg *ctx;
	char *end;
	int port;

	if (argc < 2 || 3 < argc)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &end, 0);
	if (*end || port < 0 || board_get_pchg_count() <= port)
		return EC_ERROR_PARAM2;

	ctx = &pchgs[port];

	if (argc == 2) {
		cps8100_dump(ctx);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[2], "reset")) {
		cps8x00_reset(ctx);
		cps8x00_init(ctx);
	} else {
		return EC_ERROR_PARAM3;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cps8100, cc_cps8100, "<port> [reset]",
			"Print status of or reset CPS8100");
