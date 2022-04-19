/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
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

#define CPUTS(outstr)		cputs(CC_PCHG, outstr)
#define CPRINTS(fmt, args...)	cprints(CC_PCHG, "CPS8100: " fmt, ##args)
#define CPRINTFP(fmt, args...)	cprintf(CC_PCHG, "CPS8100: " fmt, ##args)
#define CPRINTF(fmt, args...)	cprintf(CC_PCHG, fmt, ##args)

/*
 * Configuration
 */
#define CPS8100_I2C_ADDR_H		0x31
#define CPS8100_I2C_ADDR_L		0x30

/* High address registers (commands?) */
#define CPS8100_REGH_PASSWORD		0xf500
#define CPS8100_REGH_ACCESS_MODE	0xf505
#define CPS8100_REGH_ADDRESS		0xf503

#define CPS8100_ACCESS_MODE_8		0x00
#define CPS8100_ACCESS_MODE_16		0x01
#define CPS8100_ACCESS_MODE_32		0x02

/* Registers */
#define CPS8100_REG_IC_INFO		0x20000000
#define CPS8100_REG_FW_INFO		0x20000004
#define CPS8100_REG_FUNC_EN		0x2000003c
#define CPS8100_REG_ALERT_INFO		0x20000158
#define CPS8100_REG_INT_ENABLE		0x20000160
#define CPS8100_REG_INT_FLAG		0x20000164

#define CPS8100_STATUS_PROFILE(r)	(((r) & GENMASK(5, 4)) >> 4)
#define CPS8100_STATUS_CHARGE(r)	((r) & BIT(6))
#define CPS8100_STATUS_DEVICE(r)	((r) & BIT(7))
#define CPS8100_STATUS_BATTERY(r)	(((r) & GENMASK(15, 8)) >> 8)
#define CPS8100_IRQ_TYPE(r)		(((r) & GENMASK(23, 20)) >> 20)

/* Status flags in ALERT_INFO register */
#define CPS8100_STATUS_FOD		BIT(0)
#define CPS8100_STATUS_OCP		BIT(1)
#define CPS8100_STATUS_OVP		BIT(2)
#define CPS8100_STATUS_OTP		BIT(3)
#define CPS8100_STATUS_UVP		BIT(16)

/* Buffer size for i2c read & write */
#define CPS8100_MESSAGE_BUFFER_SIZE	0x20

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

/* This driver isn't compatible with big endian. */
BUILD_ASSERT(__BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__);

static const char * const cps8100_func_names[] = {
	[0] = "DPL",
	[1] = "OPP",
	[2] = "OTP",
	[3] = "OVPK",
	[4] = "OCP",
	[5] = "UVP",
	[6] = "OVP",
	[7] = "FOD",
	[8] = "SAMSUNG",
	[9] = "APPLE",
	[10] = "EPP",
	[11] = "HUAWEI",
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

static const char * const cps8100_irq_type_names[] = {
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

static const char * const cps8100_profile_names[] = {
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
		msleep(cps8100_wake_up_delay_ms);
		rv = i2c_xfer(port, addr, buf, len, NULL, 0);
	}
	if (rv)
		CPRINTS("Failed to write: %d", rv);

	return rv;
}

static int cps8100_set_unlock(int port)
{
	uint8_t buf[4];

	buf[0] = 0xf5;
	buf[1] = 0x00;	/* Password register address */
	buf[2] = 0xe5;
	buf[3] = 0x19;	/* Password */

	return cps8100_i2c_write(port, CPS8100_I2C_ADDR_H, buf, 4);
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

	if (cps8100_set_unlock(port) ||
	    cps8100_set_write_mode(port, CPS8100_ACCESS_MODE_32) ||
	    cps8100_set_high_address(port, reg))
		return EC_ERROR_UNKNOWN;

	/* Set low 16 bits of register address and read a byte. */
	buf[0] = (reg >> 8) & 0xff;
	buf[1] = (reg >> 0) & 0xff;

	return i2c_xfer(port, CPS8100_I2C_ADDR_L, buf, 2,
			(void *)val, sizeof(*val));
}

static int cps8100_reset(struct pchg *ctx)
{
	gpio_set_level(GPIO_QI_RESET_L, 0);
	cps8100_status_update(ctx, 0);
	udelay(15);
	gpio_set_level(GPIO_QI_RESET_L, 1);

	return EC_SUCCESS;
}

static int cps8100_init(struct pchg *ctx)
{
	uint32_t u32;
	int port = ctx->cfg->i2c_port;
	int rv;

	rv = cps8100_read32(port, CPS8100_REG_IC_INFO, &u32);
	if (!rv)
		CPRINTS("IC=0x%08x", u32);

	rv = cps8100_read32(port, CPS8100_REG_FW_INFO, &u32);
	if (!rv)
		CPRINTS("FW=0x%08x", u32);

	return EC_SUCCESS;
}

static int cps8100_enable(struct pchg *ctx, bool enable)
{
	return EC_SUCCESS;
}

static int cps8100_get_alert_info(struct pchg *ctx, uint32_t *reg)
{
	int rv;

	rv = cps8100_read32(ctx->cfg->i2c_port, CPS8100_REG_ALERT_INFO, reg);
	if (rv) {
		CPRINTS("Failed to get alert info (%d)", rv);
		return rv;
	}

	return EC_SUCCESS;
}

static void cps8100_print_alert_info(uint32_t reg)
{
	cps8100_print_irq_type_names("IRQ_TYPE: ", reg);
	cps8100_print_status_flag_names("ERRORS: ", reg);

	CPRINTFP("Profile: %s\n",
		 cps8100_profile_names[CPS8100_STATUS_PROFILE(reg)]);
	CPRINTFP("%sCharging\n", CPS8100_STATUS_CHARGE(reg) ? "" : "Not ");
	CPRINTFP("Device %sPresent\n", CPS8100_STATUS_DEVICE(reg) ? "":"Not ");
	CPRINTFP("Battery: %d%%\n", CPS8100_STATUS_BATTERY(reg));
}

static int cps8100_get_event(struct pchg *ctx)
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

static int cps8100_get_soc(struct pchg *ctx)
{
	ctx->battery_percent = CPS8100_STATUS_BATTERY(cps8100_state.reg);

	return EC_SUCCESS;
}

static int cps8100_update_open(struct pchg *ctx)
{
	return EC_ERROR_UNIMPLEMENTED;
}

static int cps8100_update_write(struct pchg *ctx)
{
	return EC_ERROR_UNIMPLEMENTED;
}

static int cps8100_update_close(struct pchg *ctx)
{
	return EC_ERROR_UNIMPLEMENTED;
}

const struct pchg_drv cps8100_drv = {
	.reset = cps8100_reset,
	.init = cps8100_init,
	.enable = cps8100_enable,
	.get_event = cps8100_get_event,
	.get_soc = cps8100_get_soc,
	.update_open = cps8100_update_open,
	.update_write = cps8100_update_write,
	.update_close = cps8100_update_close,
};

static void cps8100_dump(struct pchg *ctx)
{
	uint32_t val;
	int rv;

	rv = cps8100_read32(ctx->cfg->i2c_port, CPS8100_REG_FUNC_EN, &val);
	if (rv == EC_SUCCESS)
		cps8100_print_func_names("FEATURES: ", val);

	rv = cps8100_get_alert_info(ctx, &val);
	if (rv == EC_SUCCESS)
		cps8100_print_alert_info(val);
}

static int cc_cps8100(int argc, char **argv)
{
	struct pchg *ctx;
	char *end;
	int port;

	if (argc < 2 || 3 < argc)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &end, 0);
	if (*end || port < 0 || pchg_count <= port)
		return EC_ERROR_PARAM2;

	ctx = &pchgs[port];

	if (argc == 2) {
		cps8100_dump(ctx);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[2], "reset")) {
		cps8100_reset(ctx);
		cps8100_init(ctx);
	} else {
		return EC_ERROR_PARAM3;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cps8100, cc_cps8100,
			"<port> [reset]",
			"Print status of or reset CPS8100");
