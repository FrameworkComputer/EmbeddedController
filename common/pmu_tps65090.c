/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TPS65090 PMU driver.
 */

#include "board.h"
#include "clock.h"
#include "console.h"
#include "common.h"
#include "hooks.h"
#include "i2c.h"
#include "pmu_tpschrome.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

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
#define CG_CTRL0_EN   1

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


/* IRQ events */
#define EVENT_VACG    (1 << 1) /* AC voltage good */
#define EVENT_VSYSG   (1 << 2) /* System voltage good */
#define EVENT_VBATG   (1 << 3) /* Battery voltage good */
#define EVENT_CGACT   (1 << 4) /* Charging status */
#define EVENT_CGCPL   (1 << 5) /* Charging complete */

/* Charger alarm */
#define CHARGER_ALARM 3

/* Charger temperature threshold table */
static const uint8_t const pmu_temp_threshold[] = {
	1, /* 0b001,  0 degree C */
	2, /* 0b010, 10 degree C */
	5, /* 0b101, 45 degree C */
	7, /* 0b111, 60 degree C */
};

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
		CPRINTF("pmu event: %016b\n", *event);
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

int pmu_get_power_source(int *ac_good, int *battery_good)
{
	int rv, event;

	rv = pmu_get_event(&event);
	if (rv)
		return rv;

	if (ac_good)
		*ac_good = event & EVENT_VACG;
	if (battery_good)
		*battery_good = event & EVENT_VBATG;

	return EC_SUCCESS;
}

int pmu_enable_charger(int enable)
{
	int rv;
	int reg;

	rv = pmu_read(CG_CTRL0, &reg);
	if (rv)
		return rv;

	if (reg & CG_CTRL0_EN)
		return EC_SUCCESS;

	return pmu_write(CG_CTRL0, enable ? (reg | CG_CTRL0_EN) :
			(reg & ~CG_CTRL0));
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

void pmu_irq_handler(enum gpio_signal signal)
{
	/* TODO(rongchang): remove GPIO_AC_STATUS, we're not using it */
	gpio_set_level(GPIO_AC_STATUS, pmu_get_ac());

	task_wake(TASK_ID_PMU_TPS65090_CHARGER);
	CPRINTF("Charger IRQ received.\n");
}

int pmu_get_ac(void)
{
	/*
	 * Detect AC state using combined gpio pins
	 *
	 * On daisy and snow, there's no single gpio signal to detect AC.
	 *   GPIO_AC_PWRBTN_L provides AC on and PWRBTN release.
	 *   GPIO_KB_PWR_ON_L provides PWRBTN release.
	 *
	 * When AC plugged, both GPIOs will be high.
	 *
	 * One drawback of this detection is, when press-and-hold power
	 * button. AC state will be unknown. This function will fallback
	 * to PMU VACG.
	 *
	 * TODO(rongchang): move board related function to board/ and common
	 * interface to system_get_ac()
	 */

	int ac_good = 1, battery_good;

	if (gpio_get_level(GPIO_KB_PWR_ON_L))
		return gpio_get_level(GPIO_AC_PWRBTN_L);

	/* Check PMU VACG */
	if (!in_interrupt_context())
		pmu_get_power_source(&ac_good, &battery_good);

	/*
	 * Charging task only interacts with AP in discharging state. So
	 * return 1 when AC status can not be detected by GPIO or VACG.
	 */
	return ac_good;
}

int pmu_shutdown(void)
{
	int offset, rv = 0;

	/* Disable each of the DCDCs */
	for (offset = DCDC1_CTRL; offset <= DCDC3_CTRL; offset++)
		rv |= pmu_write(offset, 0x0e);
	/* Disable each of the FETs */
	for (offset = FET1_CTRL; offset <= FET7_CTRL; offset++)
		rv |= pmu_write(offset, 0x02);
	/* Clearing AD controls/status */
	rv |= pmu_write(AD_CTRL, 0x00);

	return rv ? EC_ERROR_UNKNOWN : EC_SUCCESS;
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
	for (i = 0; i < ARRAY_SIZE(reg); i++)
		pmu_write(reg[i].index, reg[i].value);
}

void pmu_init(void)
{
	/* Reset everything to default, safe values */
	pmu_init_registers();

#ifdef CONFIG_PMU_BOARD_INIT
	board_pmu_init();
#else
	/* Init configuration
	 *   Fast charge timer    : 2 hours
	 *   Charger              : disable
	 *   External pin control : enable
	 *
	 * TODO: move settings to battery pack specific init
	 */
	pmu_write(CG_CTRL0, 2);
	/* Limit full charge current to 50%
	 * TODO: remove this temporary hack.
	 */
	pmu_write(CG_CTRL3, 0xbb);
#endif
	/* Enable interrupts */
	pmu_write(IRQ1MASK,
		EVENT_VACG  | /* AC voltage good */
		EVENT_VSYSG | /* System voltage good */
		EVENT_VBATG | /* Battery voltage good */
		EVENT_CGACT | /* Charging status */
		EVENT_CGCPL); /* Charging complete */
	pmu_write(IRQ2MASK, 0);
	pmu_clear_irq();

	/* Enable charger interrupt. */
	gpio_enable_interrupt(GPIO_CHARGER_INT);

}

/* Initializes PMU when power is turned on.  This is necessary because the TPS'
 * 3.3V rail is not powered until the power is turned on. */
static int pmu_chipset_startup(void)
{
	pmu_init();
	return 0;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pmu_chipset_startup, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_PMU
static int print_pmu_info(void)
{
	int reg, ret;
	int value;

	for (reg = 0; reg < 0xc; reg++) {
		ret = pmu_read(reg, &value);
		if (ret)
			return ret;
		if (!reg)
			ccputs("PMU: ");

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
	CPRINTF("pmu events b%08b\n", value);
	CPRINTF("ac gpio    %d\n", pmu_get_ac());

	if (rv)
		ccprintf("Failed - error %d\n", rv);

	return rv ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pmu, command_pmu,
			"<repeat_count>",
			"Print PMU info",
			NULL);
#endif
