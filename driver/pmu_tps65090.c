/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TPS65090 PMU driver.
 */

#include "clock.h"
#include "console.h"
#include "common.h"
#include "extpower.h"
#include "host_command.h"
#include "hooks.h"
#include "i2c.h"
#include "pmu_tpschrome.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define TPS65090_I2C_ADDR 0x90

#define IRQ1_REG 0x00
#define IRQ2_REG 0x01
#define IRQ1MASK 0x02
#define IRQ2MASK 0x03
#define CG_CTRL0 0x04
#define CG_CTRL1 0x05
#define CG_CTRL2 0x06
#define CG_CTRL3 0x07
#define CG_CTRL4 0x08
#define CG_CTRL5 0x09
#define CG_STATUS1 0x0a
#define CG_STATUS2 0x0b
#define DCDC1_CTRL 0x0c
#define DCDC2_CTRL 0x0d
#define DCDC3_CTRL 0x0e
#define FET1_CTRL 0x0f
#define FET2_CTRL 0x10
#define FET3_CTRL 0x11
#define FET4_CTRL 0x12
#define FET5_CTRL 0x13
#define FET6_CTRL 0x14
#define FET7_CTRL 0x15
#define AD_CTRL 0x16
#define AD_OUT1 0x17
#define AD_OUT2 0x18
#define TPSCHROME_VER 0x19

/* Charger control */
#define CG_EN               (1 << 0)
#define CG_EXT_EN           (1 << 1)
#define CG_FASTCHARGE_SHIFT 2
#define CG_FASTCHARGE_MASK  (7 << CG_FASTCHARGE_SHIFT)

/* Charger termination voltage/current */
#define CG_VSET_SHIFT   3
#define CG_VSET_MASK    (3 << CG_VSET_SHIFT)
#define CG_ISET_SHIFT   0
#define CG_ISET_MASK    (7 << CG_ISET_SHIFT)
#define CG_NOITERM      (1 << 5)
#define CG_TSET_SHIFT   5
#define CG_TSET_MASK    (7 << CG_TSET_SHIFT)

/* A temperature threshold to force charger hardware error */
#define CG_TEMP_THRESHOLD_ERROR 0

/* Timeout indication */
#define STATUS_TIMEOUT_MASK       0xc
#define STATUS_PRECHARGE_TIMEOUT  0x4
#define STATUS_FASTCHARGE_TIMEOUT 0x8

/* IRQ events */
#define EVENT_VACG    (1 << 1) /* AC voltage good */
#define EVENT_VSYSG   (1 << 2) /* System voltage good */
#define EVENT_VBATG   (1 << 3) /* Battery voltage good */
#define EVENT_CGACT   (1 << 4) /* Charging status */
#define EVENT_CGCPL   (1 << 5) /* Charging complete */

/* Charger alarm */
#define CHARGER_ALARM 3

/* FET control register bits */
#define FET_CTRL_ENFET   (1 << 0)
#define FET_CTRL_ADENFET (1 << 1)
#define FET_CTRL_WAIT    (3 << 2) /* Overcurrent timeout max : 3200 us */
#define FET_CTRL_PGFET   (1 << 4)

#define FET_CTRL_BASE (FET1_CTRL - 1)

#define POWER_GOOD_DELAY_US 3500

/* AD control register bits */
#define AD_CTRL_ENADREF  (1 << 4)
#define AD_CTRL_ADEOC    (1 << 5)
#define AD_CTRL_ADSTART  (1 << 6)

#define HARD_RESET_TIMEOUT_MS 5

/* Charger temperature threshold table */
static const uint8_t const pmu_temp_threshold[] = {
	1, /* 0b001,  0 degree C */
	2, /* 0b010, 10 degree C */
	5, /* 0b101, 45 degree C */
	7, /* 0b111, 60 degree C */
};

#ifdef CONFIG_PMU_HARD_RESET
/**
 * Force the pmic to reset completely.
 *
 * This forces an entire system reset, and therefore should never return.  The
 * implementation is rather hacky; it simply shorts out the 3.3V rail to force
 * the PMIC to panic.  We need this unfortunate hack because it's the only way
 * to reset the I2C engine inside the PMU.
 */
static void pmu_hard_reset(void)
{
	/* Short out the 3.3V rail to force a hard reset of tps Chrome */
	gpio_set_level(GPIO_PMIC_RESET, 1);

	/* Delay while the power is cut */
	udelay(HARD_RESET_TIMEOUT_MS * 1000);

	/* Shouldn't get here unless the board doesn't have this capability */
	panic_puts("pmu hard reset failed! (this board may not be capable)\n");
}
#else
static void pmu_hard_reset(void)
{
	panic_puts("pmu hard reset unsupported!\n");
}
#endif

/* Read all tps65090 interrupt events */
static int pmu_get_event(int *event)
{
	static int prev_event;
	int rv;
	int irq1, irq2;

	pmu_clear_irq();

	rv = pmu_read(IRQ1_REG, &irq1);
	if (rv)
		return rv;
	rv = pmu_read(IRQ2_REG, &irq2);
	if (rv)
		return rv;

	*event = irq1 | (irq2 << 8);

	if (prev_event != *event) {
		CPRINTS("pmu event: %016b", *event);
		prev_event = *event;
	}

	return EC_SUCCESS;
}

/* Clear tps65090 irq */
int pmu_clear_irq(void)
{
	return pmu_write(IRQ1_REG, 0);
}

/* Read/write tps65090 register */
int pmu_read(int reg, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, TPS65090_I2C_ADDR, reg, value);
}

int pmu_write(int reg, int value)
{
	return i2c_write8(I2C_PORT_CHARGER, TPS65090_I2C_ADDR, reg, value);
}

/**
 * Read tpschrome version
 *
 * @param version       output value of tpschrome version
 */
int pmu_version(int *version)
{
	return pmu_read(TPSCHROME_VER, version);
}

int pmu_is_charger_alarm(void)
{
	int status;

	/**
	 * if the I2C access to the PMU fails, we consider the failure as
	 * non-critical and wait for the next read without send the alert.
	 */
	if (!pmu_read(CG_STATUS1, &status) && (status & CHARGER_ALARM))
		return 1;
	return 0;
}

int pmu_is_charge_timeout(void)
{
	int status;

	if (pmu_read(CG_STATUS1, &status))
		return 0;

	status &= STATUS_TIMEOUT_MASK;
	return (status == STATUS_PRECHARGE_TIMEOUT) ||
	       (status == STATUS_FASTCHARGE_TIMEOUT);
}

int pmu_get_power_source(int *ac_good, int *battery_good)
{
	int rv, event = 0;

	rv = pmu_get_event(&event);
	if (rv)
		return rv;

	if (ac_good)
		*ac_good = event & EVENT_VACG;
	if (battery_good)
		*battery_good = event & EVENT_VBATG;

	return EC_SUCCESS;
}

/**
 * Enable charger's charging function
 *
 * When enable, charger ignores external control and charge the
 * battery directly. If EC wants to contorl charging, set the flag
 * to 0.
 */
int pmu_enable_charger(int enable)
{
	int rv;
	int reg;

	rv = pmu_read(CG_CTRL0, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= CG_EN;
	else
		reg &= ~CG_EN;

	return pmu_write(CG_CTRL0, reg);
}

/**
 * Set external charge enable pin
 *
 * @param enable        boolean, set 1 to eanble external control
 */
int pmu_enable_ext_control(int enable)
{
	int rv;
	int reg;

	rv = pmu_read(CG_CTRL0, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= CG_EXT_EN;
	else
		reg &= ~CG_EXT_EN;

	return pmu_write(CG_CTRL0, reg);
}

/**
 * Set fast charge timeout
 *
 * @param timeout         enum FASTCHARGE_TIMEOUT
 */
int pmu_set_fastcharge(enum FASTCHARGE_TIMEOUT timeout)
{
	int rv;
	int reg;

	rv = pmu_read(CG_CTRL0, &reg);
	if (rv)
		return rv;

	reg &= ~CG_FASTCHARGE_MASK;
	reg |= (timeout << CG_FASTCHARGE_SHIFT) & CG_FASTCHARGE_MASK;

	return pmu_write(CG_CTRL0, reg);
}

/**
 * Set termination current for temperature ranges
 *
 * @param range           T01 T12 T23 T34 T40
 * @param current         enum termination current, I0250 == 25.0%:
 *                        I0000 I0250 I0375 I0500 I0625 I0750 I0875 I1000
 */
int pmu_set_term_current(enum TPS_TEMPERATURE_RANGE range,
		enum TPS_TERMINATION_CURRENT current)
{
	int rv;
	int reg_val;

	rv = pmu_read(CG_CTRL1 + range, &reg_val);
	if (rv)
		return rv;

	reg_val &= ~CG_ISET_MASK;
	reg_val |= current << CG_ISET_SHIFT;

	return pmu_write(CG_CTRL1 + range, reg_val);
}

/**
 * Set termination voltage for temperature ranges
 *
 * @param range           T01 T12 T23 T34 T40
 * @param voltage         enum termination voltage, V2050 == 2.05V:
 *                        V2000 V2050 V2075 V2100
 */
int pmu_set_term_voltage(enum TPS_TEMPERATURE_RANGE range,
		enum TPS_TERMINATION_VOLTAGE voltage)
{
	int rv;
	int reg_val;

	rv = pmu_read(CG_CTRL1 + range, &reg_val);
	if (rv)
		return rv;

	reg_val &= ~CG_VSET_MASK;
	reg_val |= voltage << CG_VSET_SHIFT;

	return pmu_write(CG_CTRL1 + range, reg_val);
}

/**
 * Set temperature threshold
 *
 * @param temp_n          TSET_T1 to TSET_T4
 * @param value           0b000 ~ 0b111, temperature threshold
 */
int pmu_set_temp_threshold(enum TPS_TEMPERATURE temp_n, uint8_t value)
{
	int rv;
	int reg_val;

	/*
	 * Temperature threshold T1 to T4 are stored in TPSCHROME registers
	 * CG_CTRL1 to CG_CTRL4.
	 */
	rv = pmu_read(CG_CTRL1 + temp_n, &reg_val);
	if (rv)
		return rv;

	reg_val &= ~CG_TSET_MASK;
	reg_val |= (value << CG_TSET_SHIFT) & CG_TSET_MASK;

	return pmu_write(CG_CTRL1 + temp_n, reg_val);
}

/**
 * Force charger into error state, turn off charging and blinks charging LED
 *
 * @param enable          true to turn off charging and blink LED
 * @return                EC_SUCCESS for success
 */
int pmu_blink_led(int enable)
{
	int rv;
	enum TPS_TEMPERATURE t;
	uint8_t threshold;

	for (t = TSET_T1; t <= TSET_T4; t++) {
		if (enable)
			threshold = CG_TEMP_THRESHOLD_ERROR;
		else
			threshold = pmu_temp_threshold[t];

		rv = pmu_set_temp_threshold(t, threshold);
		if (rv) {
			/* Retry */
			rv = pmu_set_temp_threshold(t, threshold);
			if (rv)
				return rv;
		}
	}

	return EC_SUCCESS;
}

/**
 * Enable low current charging
 *
 * @param enable         enable/disable low current charging
 */
int pmu_low_current_charging(int enable)
{
	int rv;
	int reg_val;

	rv = pmu_read(CG_CTRL5, &reg_val);
	if (rv)
		return rv;

	if (enable)
		reg_val |= CG_NOITERM;
	else
		reg_val &= ~CG_NOITERM;

	return pmu_write(CG_CTRL5, reg_val);
}

int pmu_enable_fet(int fet_id, int enable, int *power_good)
{
	int rv, reg;
	int reg_offset;

	reg_offset = FET_CTRL_BASE + fet_id;

	rv = pmu_read(reg_offset, &reg);
	if (rv)
		return rv;
	reg |= FET_CTRL_ADENFET | FET_CTRL_WAIT;
	if (enable)
		reg |= FET_CTRL_ENFET;
	else
		reg &= ~FET_CTRL_ENFET;

	rv = pmu_write(reg_offset, reg);
	if (rv)
		return rv;

	if (power_good) {
		usleep(POWER_GOOD_DELAY_US);
		rv = pmu_read(reg_offset, &reg);
		if (rv)
			return rv;
		*power_good = reg & FET_CTRL_PGFET;
	}

	return EC_SUCCESS;
}

int pmu_adc_read(int adc_idx, int flags)
{
	int ctrl;
	int val1, val2;
	int rv;

	rv = pmu_read(AD_CTRL, &ctrl);
	if (rv)
		return rv;
	if (!(ctrl & AD_CTRL_ENADREF)) {
		ctrl |= AD_CTRL_ENADREF;
		rv = pmu_write(AD_CTRL, ctrl);
		if (rv)
			return rv;
		/* wait for reference voltage stabilization */
		msleep(10);
	}

	ctrl = (ctrl & ~0xf) | adc_idx;
	rv = pmu_write(AD_CTRL, ctrl);
	if (rv)
		return rv;
	udelay(150);

	ctrl |= AD_CTRL_ADSTART;
	rv = pmu_write(AD_CTRL, ctrl);
	if (rv)
		return rv;
	udelay(200);

	do {
		rv = pmu_read(AD_CTRL, &ctrl);
		if (rv)
			return rv;
	} while (!(ctrl & AD_CTRL_ADEOC));

	rv = pmu_read(AD_OUT1, &val1) | pmu_read(AD_OUT2, &val2);
	if (rv)
		return rv;

	if (!(flags & ADC_FLAG_KEEP_ON))
		rv = pmu_write(AD_CTRL, ctrl & ~AD_CTRL_ENADREF);

	return (val2 << 8) | val1;
}

/**
 * Attempt shutdown.
 */
static int pmu_try_shutdown(void)
{
	int offset;

	/* Disable each of the DCDCs */
	for (offset = DCDC1_CTRL; offset <= DCDC3_CTRL; offset++) {
		if (pmu_write(offset, 0x0e))
			return EC_ERROR_UNKNOWN;
	}
	/* Disable each of the FETs */
	for (offset = FET1_CTRL; offset <= FET7_CTRL; offset++) {
		if (pmu_write(offset, 0x02))
			return EC_ERROR_UNKNOWN;
	}

	/* Clear AD controls/status */
	if (pmu_write(AD_CTRL, 0x00))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

int pmu_shutdown(void)
{
	int pmu_shutdown_retries = 3;

	/* Attempt shutdown */
	while (--pmu_shutdown_retries >= 0) {
		if (!pmu_try_shutdown())
			return EC_SUCCESS;
	}

#ifdef CONFIG_PMU_HARD_RESET
	/* We ran out of tries, so reset the board */
	CPUTS("PMU shutdown failed. Hard-resetting.\n");
	cflush();
	pmu_hard_reset();
#endif

	/* If we're still here, we couldn't shutdown OR reset */
	return EC_ERROR_UNKNOWN;
}

/*
 * Fill all of the pmu registers with known good values, this allows the
 * pmu to recover by rebooting the system if its registers were trashed.
 */
static void pmu_init_registers(void)
{
	const struct {
		uint8_t index;
		uint8_t value;
	} reg[] = {
		{IRQ1MASK, 0x00},
		{IRQ2MASK, 0x00},
		{CG_CTRL0, 0x02},
		{CG_CTRL1, 0x20},
		{CG_CTRL2, 0x4b},
		{CG_CTRL3, 0xbf},
		{CG_CTRL4, 0xf3},
		{CG_CTRL5, 0xc0},
		{DCDC1_CTRL, 0x0e},
		{DCDC2_CTRL, 0x0e},
		{DCDC3_CTRL, 0x0e},
		{FET1_CTRL, 0x02},
		{FET2_CTRL, 0x02},
		{FET3_CTRL, 0x02},
		{FET4_CTRL, 0x02},
		{FET5_CTRL, 0x02},
		{FET6_CTRL, 0x02},
		{FET7_CTRL, 0x02},
		{AD_CTRL, 0x00},
		{IRQ1_REG, 0x00}
	};
	uint8_t i;

	/*
	 * Write all PMU registers.  Ignore return value from pmu_write()
	 * because there's nothing we can reasonably do if it fails.
	 */
	for (i = 0; i < ARRAY_SIZE(reg); i++)
		pmu_write(reg[i].index, reg[i].value);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, pmu_init_registers, HOOK_PRIO_DEFAULT);

void pmu_init(void)
{
	int failure = 0, retries_remaining = 3;

	while (--retries_remaining >= 0) {
		failure = pmu_board_init();

		/* Enable interrupts */
		if (!failure) {
			failure = pmu_write(IRQ1MASK,
					EVENT_VACG  | /* AC voltage good */
					EVENT_VSYSG | /* System voltage good */
					EVENT_VBATG | /* Battery voltage good */
					EVENT_CGACT | /* Charging status */
					EVENT_CGCPL); /* Charging complete */
		}
		if (!failure)
			failure = pmu_write(IRQ2MASK, 0);
		if (!failure)
			failure = pmu_clear_irq();

		/* Exit the retry loop if there was no failure */
		if (!failure)
			break;
	}

	if (failure) {
		CPUTS("Failed to initialize PMU. Hard-resetting.\n");
		cflush();
		pmu_hard_reset();
	}
}

/* Initializes PMU when power is turned on.  This is necessary because the TPS'
 * 3.3V rail is not powered until the power is turned on. */
static void pmu_chipset_startup(void)
{
	pmu_init();

#ifdef BOARD_PIT
	/* Enable FET4 by default which allows for SD Card booting */
	{
		int pgood;
		pmu_enable_fet(4, 1, &pgood);
	}
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pmu_chipset_startup, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_PMU
static int print_pmu_info(void)
{
	int reg, ret;
	int value;

	ccprintf("     ");
	for (reg = 0; reg <= 0x18; reg++)
		ccprintf("%02x ", reg);
	ccprintf("\n");

	ccprintf("PMU: ");
	for (reg = 0; reg <= 0x18; reg++) {
		ret = pmu_read(reg, &value);
		if (ret)
			return ret;
		ccprintf("%02x ", value);
	}
	ccputs("\n");

	return 0;
}

static int command_pmu(int argc, char **argv)
{
	int repeat = 1;
	int rv = 0;
	int loop;
	int value;
	char *e;

	if (argc > 1) {
		repeat = strtoi(argv[1], &e, 0);
		if (*e) {
			if (strlen(argv[1]) >= 1 && argv[1][0] == 'r') {
				pmu_hard_reset();
				/* If this returns, there was an error */
				return EC_ERROR_UNKNOWN;
			}

			ccputs("Invalid repeat count\n");
			return EC_ERROR_INVAL;
		}
	}

	for (loop = 0; loop < repeat; loop++) {
		rv = print_pmu_info();
		usleep(1000);
	}

	rv = pmu_read(IRQ1_REG, &value);
	if (rv)
		return rv;
	CPRINTS("pmu events b%08b", value);
	CPRINTS("ac gpio    %d", extpower_is_present());

	if (rv)
		ccprintf("Failed - error %d\n", rv);

	return rv ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pmu, command_pmu,
			"<repeat_count|reset>",
			"Print PMU info or force a hard reset",
			NULL);
#endif

/*****************************************************************************/
/* TPSchrome LDO pass-through
 */
#ifdef CONFIG_I2C_PASSTHROUGH
static int host_command_ldo_get(struct host_cmd_handler_args *args)
{
	int rv;
	int val;
	const struct ec_params_ldo_get *p = args->params;
	struct ec_response_ldo_get *r = args->response;

	/* is this an existing TPSchrome FET ? */
	if ((p->index < 1) || (p->index > 7))
		return EC_RES_ERROR;

	rv = pmu_read(FET_CTRL_BASE + p->index, &val);
	if (rv)
		return EC_RES_ERROR;

	r->state = !!(val & FET_CTRL_PGFET);
	args->response_size = sizeof(struct ec_response_ldo_get);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LDO_GET,
		     host_command_ldo_get,
		     EC_VER_MASK(0));

static int host_command_ldo_set(struct host_cmd_handler_args *args)
{
	int rv;
	const struct ec_params_ldo_set *p = args->params;

	/* is this an existing TPSchrome FET ? */
	if ((p->index < 1) || (p->index > 7))
		return EC_RES_ERROR;
	rv = pmu_enable_fet(p->index, p->state & EC_LDO_STATE_ON, NULL);

	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_LDO_SET,
		     host_command_ldo_set,
		     EC_VER_MASK(0));
#endif
