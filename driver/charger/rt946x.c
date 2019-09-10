/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Richtek rt946x, Mediatek mt6370 battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "charge_manager.h"
#include "common.h"
#include "compile_time_macros.h"
#include "config.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "printf.h"
#include "driver/wpc/p9221.h"
#include "rt946x.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, "CHG " format, ## args)


/* Charger parameters */
static const struct charger_info rt946x_charger_info = {
	.name         = CHARGER_NAME,
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = CHARGE_I_MAX,
	.current_min  = CHARGE_I_MIN,
	.current_step = CHARGE_I_STEP,
	.input_current_max  = INPUT_I_MAX,
	.input_current_min  = INPUT_I_MIN,
	.input_current_step = INPUT_I_STEP,
};

static const struct rt946x_init_setting default_init_setting = {
	.eoc_current = 400,
	.mivr = 4000,
	.ircmp_vclamp = 32,
	.ircmp_res = 25,
	.boost_voltage = 5050,
	.boost_current = 1500,
};

__attribute__((weak))
const struct rt946x_init_setting *board_rt946x_init_setting(void)
{
	return &default_init_setting;
}

enum rt946x_ilmtsel {
	RT946X_ILMTSEL_PSEL_OTG,
	RT946X_ILMTSEL_AICR = 2,
	RT946X_ILMTSEL_LOWER_LEVEL, /* lower of above two */
};

enum rt946x_chg_stat {
	RT946X_CHGSTAT_READY = 0,
	RT946X_CHGSTAT_IN_PROGRESS,
	RT946X_CHGSTAT_DONE,
	RT946X_CHGSTAT_FAULT,
};

enum rt946x_adc_in_sel {
	RT946X_ADC_VBUS_DIV5 = 1,
	RT946X_ADC_VBUS_DIV2,
};

#if defined(CONFIG_CHARGER_RT9466) || defined(CONFIG_CHARGER_RT9467)
enum rt946x_irq {
	RT946X_IRQ_CHGSTATC = 0,
	RT946X_IRQ_CHGFAULT,
	RT946X_IRQ_TSSTATC,
	RT946X_IRQ_CHGIRQ1,
	RT946X_IRQ_CHGIRQ2,
	RT946X_IRQ_CHGIRQ3,
#ifdef CONFIG_CHARGER_RT9467
	RT946X_IRQ_DPDMIRQ,
#endif
	RT946X_IRQ_COUNT,
};

static uint8_t rt946x_irqmask[RT946X_IRQ_COUNT] = {
	0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF,
#ifdef CONFIG_CHARGER_RT9467
	0xFC,
#endif
};

static const uint8_t rt946x_irq_maskall[RT946X_IRQ_COUNT] = {
	0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF,
#ifdef CONFIG_CHARGER_RT9467
	0xFF,
#endif
};
#elif defined(CONFIG_CHARGER_MT6370)
enum rt946x_irq {
	MT6370_IRQ_CHGSTAT1 = 0,
	MT6370_IRQ_CHGSTAT2,
	MT6370_IRQ_CHGSTAT3,
	MT6370_IRQ_CHGSTAT4,
	MT6370_IRQ_CHGSTAT5,
	MT6370_IRQ_CHGSTAT6,
	MT6370_IRQ_DPDMSTAT,
	MT6370_IRQ_DICHGSTAT,
	MT6370_IRQ_OVPCTRLSTAT,
	MT6370_IRQ_FLEDSTAT1,
	MT6370_IRQ_FLEDSTAT2,
	MT6370_IRQ_BASESTAT,
	MT6370_IRQ_LDOSTAT,
	MT6370_IRQ_RGBSTAT,
	MT6370_IRQ_BLSTAT,
	MT6370_IRQ_DBSTAT,
	RT946X_IRQ_COUNT,
};

static uint8_t rt946x_irqmask[RT946X_IRQ_COUNT] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFC, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF,
};

static const uint8_t rt946x_irq_maskall[RT946X_IRQ_COUNT] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF,
};
#endif

/* Must be in ascending order */
static const uint16_t rt946x_boost_current[] = {
	500, 700, 1100, 1300, 1800, 2100, 2400,
};

static int rt946x_read8(int reg, int *val)
{
	return i2c_read8(I2C_PORT_CHARGER, RT946X_ADDR_FLAGS, reg, val);
}

static int rt946x_write8(int reg, int val)
{
	return i2c_write8(I2C_PORT_CHARGER, RT946X_ADDR_FLAGS, reg, val);
}

static int rt946x_block_write(int reg, const uint8_t *val, int len)
{
	return i2c_write_block(I2C_PORT_CHARGER, RT946X_ADDR_FLAGS,
			       reg, val, len);
}

static int rt946x_update_bits(int reg, int mask, int val)
{
	int rv;
	int reg_val = 0;

	rv = rt946x_read8(reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = rt946x_write8(reg, reg_val);
	return rv;
}

static inline int rt946x_set_bit(int reg, int mask)
{
	return rt946x_update_bits(reg, mask, mask);
}

static inline int rt946x_clr_bit(int reg, int mask)
{
	return rt946x_update_bits(reg, mask, 0x00);
}

static inline uint8_t rt946x_closest_reg(uint16_t min, uint16_t max,
					 uint16_t step, uint16_t target)
{
	if (target < min)
		return 0;
	if (target >= max)
		return ((max - min) / step);
	return (target - min) / step;
}

static int rt946x_chip_rev(int *chip_rev)
{
	int rv;

	rv = rt946x_read8(RT946X_REG_DEVICEID, chip_rev);
	if (rv == EC_SUCCESS)
		*chip_rev &= RT946X_MASK_CHIP_REV;
	return rv;
}

static inline int rt946x_enable_wdt(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_CHGCTRL13, RT946X_MASK_WDT_EN);
}

/* Enable high-impedance mode */
static inline int rt946x_enable_hz(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_CHGCTRL1, RT946X_MASK_HZ_EN);
}

int rt946x_por_reset(void)
{
	int rv, val;

#ifdef CONFIG_CHARGER_MT6370
	/* Soft reset. It takes only 1ns for resetting. b/116682788 */
	val = RT946X_MASK_SOFT_RST;
	/*
	 * MT6370 has to set passcodes before resetting all the registers and
	 * logics.
	 */
	rv = rt946x_write8(MT6370_REG_RSTPASCODE1, MT6370_MASK_RSTPASCODE1);
	rv |= rt946x_write8(MT6370_REG_RSTPASCODE2, MT6370_MASK_RSTPASCODE2);
#else
	/* Hard reset, may take several milliseconds. */
	val = RT946X_MASK_RST;
	rv = rt946x_enable_hz(0);
#endif
	if (rv)
		return rv;

	return rt946x_set_bit(RT946X_REG_CORECTRL_RST, val);
}

static int rt946x_reset_to_zero(void)
{
	int rv = 0;

	rv = charger_set_current(0);
	if (rv)
		return rv;

	rv = charger_set_voltage(0);
	if (rv)
		return rv;

	return rt946x_enable_hz(1);
}

static int rt946x_enable_bc12_detection(int en)
{
#if defined(CONFIG_CHARGER_RT9467) || defined(CONFIG_CHARGER_MT6370)
#ifdef CONFIG_CHARGER_MT6370_BC12_GPIO
	gpio_set_level(GPIO_BC12_DET_EN, en);
#endif /* CONFIG_CHARGER_MT6370_BC12_GPIO */
	return (en ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_DPDM1, RT946X_MASK_USBCHGEN);
#endif
	return 0;
}

static int rt946x_set_ieoc(unsigned int ieoc)
{
	uint8_t reg_ieoc = 0;

	reg_ieoc = rt946x_closest_reg(RT946X_IEOC_MIN, RT946X_IEOC_MAX,
		RT946X_IEOC_STEP, ieoc);

	CPRINTF("%s ieoc = %d(0x%02X)\n", __func__, ieoc, reg_ieoc);

	return rt946x_update_bits(RT946X_REG_CHGCTRL9, RT946X_MASK_IEOC,
		reg_ieoc << RT946X_SHIFT_IEOC);
}

static int rt946x_set_mivr(unsigned int mivr)
{
	uint8_t reg_mivr = 0;

	reg_mivr = rt946x_closest_reg(RT946X_MIVR_MIN, RT946X_MIVR_MAX,
		RT946X_MIVR_STEP, mivr);

	CPRINTF("%s: mivr = %d(0x%02X)\n", __func__, mivr, reg_mivr);

	return rt946x_update_bits(RT946X_REG_CHGCTRL6, RT946X_MASK_MIVR,
		reg_mivr << RT946X_SHIFT_MIVR);
}

static int rt946x_set_boost_voltage(unsigned int voltage)
{
	uint8_t reg_voltage = 0;

	reg_voltage = rt946x_closest_reg(RT946X_BOOST_VOLTAGE_MIN,
		RT946X_BOOST_VOLTAGE_MAX, RT946X_BOOST_VOLTAGE_STEP, voltage);

	CPRINTF("%s voltage = %d(0x%02X)\n", __func__, voltage, reg_voltage);

	return rt946x_update_bits(RT946X_REG_CHGCTRL5,
		RT946X_MASK_BOOST_VOLTAGE,
		reg_voltage << RT946X_SHIFT_BOOST_VOLTAGE);
}

static int rt946x_set_boost_current(unsigned int current)
{
	int i;

	/*
	 * Find the smallest output current threshold which can support
	 * our requested output current. Use the greatest achievable
	 * boost current (2.4A) if requested current is too large.
	 */
	for (i = 0; i < ARRAY_SIZE(rt946x_boost_current) - 1; i++) {
		if (current < rt946x_boost_current[i])
			break;
	}

	CPRINTF("%s current = %d(0x%02X)\n", __func__, current, i);

	return rt946x_update_bits(RT946X_REG_CHGCTRL10,
		RT946X_MASK_BOOST_CURRENT,
		i << RT946X_SHIFT_BOOST_CURRENT);
}

static int rt946x_set_ircmp_vclamp(unsigned int vclamp)
{
	uint8_t reg_vclamp = 0;

	reg_vclamp = rt946x_closest_reg(RT946X_IRCMP_VCLAMP_MIN,
		RT946X_IRCMP_VCLAMP_MAX, RT946X_IRCMP_VCLAMP_STEP, vclamp);

	CPRINTF("%s: vclamp = %d(0x%02X)\n", __func__, vclamp, reg_vclamp);

	return rt946x_update_bits(RT946X_REG_CHGCTRL18,
		RT946X_MASK_IRCMP_VCLAMP,
		reg_vclamp << RT946X_SHIFT_IRCMP_VCLAMP);
}

static int rt946x_set_ircmp_res(unsigned int res)
{
	uint8_t reg_res = 0;

	reg_res = rt946x_closest_reg(RT946X_IRCMP_RES_MIN, RT946X_IRCMP_RES_MAX,
		RT946X_IRCMP_RES_STEP, res);

	CPRINTF("%s: res = %d(0x%02X)\n", __func__, res, reg_res);

	return rt946x_update_bits(RT946X_REG_CHGCTRL18, RT946X_MASK_IRCMP_RES,
		reg_res << RT946X_SHIFT_IRCMP_RES);
}

static int rt946x_set_vprec(unsigned int vprec)
{
	uint8_t reg_vprec = 0;

	reg_vprec = rt946x_closest_reg(RT946X_VPREC_MIN, RT946X_VPREC_MAX,
		RT946X_VPREC_STEP, vprec);

	CPRINTF("%s: vprec = %d(0x%02X)\n", __func__, vprec, reg_vprec);

	return rt946x_update_bits(RT946X_REG_CHGCTRL8, RT946X_MASK_VPREC,
		reg_vprec << RT946X_SHIFT_VPREC);
}

static int rt946x_set_iprec(unsigned int iprec)
{
	uint8_t reg_iprec = 0;

	reg_iprec = rt946x_closest_reg(RT946X_IPREC_MIN, RT946X_IPREC_MAX,
		RT946X_IPREC_STEP, iprec);

	CPRINTF("%s: iprec = %d(0x%02X)\n", __func__, iprec, reg_iprec);

	return rt946x_update_bits(RT946X_REG_CHGCTRL8, RT946X_MASK_IPREC,
		reg_iprec << RT946X_SHIFT_IPREC);
}

static int rt946x_init_irq(void)
{
	int rv = 0;
	int dummy;
	int i;

	/* Mask all interrupts */
	rv = rt946x_block_write(RT946X_REG_CHGSTATCCTRL, rt946x_irq_maskall,
				RT946X_IRQ_COUNT);
	if (rv)
		return rv;

	/* Clear all interrupt flags */
	for (i = 0; i < RT946X_IRQ_COUNT; i++) {
		rv = rt946x_read8(RT946X_REG_CHGSTATC + i, &dummy);
		if (rv)
			return rv;
	}

	/* Init interrupt */
	return rt946x_block_write(RT946X_REG_CHGSTATCCTRL, rt946x_irqmask,
				  ARRAY_SIZE(rt946x_irqmask));
}

static int rt946x_init_setting(void)
{
	int rv = 0;
	const struct battery_info *batt_info = battery_get_info();
	const struct rt946x_init_setting *setting = board_rt946x_init_setting();

#ifdef CONFIG_CHARGER_OTG
	/*  Disable boost-mode output voltage */
	rv = charger_enable_otg_power(0);
	if (rv)
		return rv;
#endif
	/* Enable/Disable BC 1.2 detection */
#ifdef HAS_TASK_USB_CHG
	rv = rt946x_enable_bc12_detection(1);
#else
	rv = rt946x_enable_bc12_detection(0);
#endif
	if (rv)
		return rv;
	/* Disable WDT */
	rv = rt946x_enable_wdt(0);
	if (rv)
		return rv;
	/* Disable battery thermal protection */
	rv = rt946x_clr_bit(RT946X_REG_CHGCTRL16, RT946X_MASK_JEITA_EN);
	if (rv)
		return rv;
	/* Disable charge timer */
	rv = rt946x_clr_bit(RT946X_REG_CHGCTRL12, RT946X_MASK_TMR_EN);
	if (rv)
		return rv;
	rv = rt946x_set_mivr(setting->mivr);
	if (rv)
		return rv;
	rv = rt946x_set_ieoc(setting->eoc_current);
	if (rv)
		return rv;
	rv = rt946x_set_boost_voltage(
		setting->boost_voltage);
	if (rv)
		return rv;
	rv = rt946x_set_boost_current(
		setting->boost_current);
	if (rv)
		return rv;
	rv = rt946x_set_ircmp_vclamp(setting->ircmp_vclamp);
	if (rv)
		return rv;
	rv = rt946x_set_ircmp_res(setting->ircmp_res);
	if (rv)
		return rv;
	rv = rt946x_set_vprec(batt_info->precharge_voltage ?
			batt_info->precharge_voltage : batt_info->voltage_min);
	if (rv)
		return rv;
	rv = rt946x_set_iprec(batt_info->precharge_current);
	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_MT6370_BACKLIGHT
	rt946x_write8(MT6370_BACKLIGHT_BLEN,
		      MT6370_MASK_BLED_EXT_EN | MT6370_MASK_BLED_EN |
			MT6370_MASK_BLED_1CH_EN | MT6370_MASK_BLED_2CH_EN |
			MT6370_MASK_BLED_3CH_EN | MT6370_MASK_BLED_4CH_EN |
			MT6370_BLED_CODE_LINEAR);
	rt946x_update_bits(MT6370_BACKLIGHT_BLPWM, MT6370_MASK_BLPWM_BLED_PWM,
			   BIT(MT6370_SHIFT_BLPWM_BLED_PWM));
#endif

	return rt946x_init_irq();
}

#ifdef CONFIG_CHARGER_OTG
int charger_enable_otg_power(int enabled)
{
	return (enabled ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_CHGCTRL1, RT946X_MASK_OPA_MODE);
}

int charger_is_sourcing_otg_power(int port)
{
	int val;

	if (rt946x_read8(RT946X_REG_CHGCTRL1, &val))
		return 0;

	return !!(val & RT946X_MASK_OPA_MODE);
}
#endif

int charger_set_input_current(int input_current)
{
	uint8_t reg_iin = 0;
	const struct charger_info * const info = charger_get_info();

	reg_iin = rt946x_closest_reg(info->input_current_min,
		info->input_current_max, info->input_current_step,
		input_current);

	CPRINTF("%s iin = %d(0x%02X)\n", __func__, input_current, reg_iin);

	return rt946x_update_bits(RT946X_REG_CHGCTRL3, RT946X_MASK_AICR,
		reg_iin << RT946X_SHIFT_AICR);
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int val = 0;
	const struct charger_info * const info = charger_get_info();

	rv = rt946x_read8(RT946X_REG_CHGCTRL3, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_AICR) >> RT946X_SHIFT_AICR;
	*input_current = val * info->input_current_step
		+ info->input_current_min;

	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_device_id(int *id)
{
	int rv;

	rv = rt946x_read8(RT946X_REG_DEVICEID, id);
	if (rv == EC_SUCCESS)
		*id &= RT946X_MASK_VENDOR_ID;
	return rv;
}

int charger_get_option(int *option)
{
	/* Ignored: does not exist */
	*option = 0;
	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	/* Ignored: does not exist */
	return EC_SUCCESS;
}

const struct charger_info *charger_get_info(void)
{
	return &rt946x_charger_info;
}

int charger_get_status(int *status)
{
	int rv;
	int val = 0;

	rv = rt946x_read8(RT946X_REG_CHGCTRL2, &val);
	if (rv)
		return rv;
	val = (val & RT946X_MASK_CHG_EN) >> RT946X_SHIFT_CHG_EN;
	if (!val)
		*status |= CHARGER_CHARGE_INHIBITED;

	rv = rt946x_read8(RT946X_REG_CHGFAULT, &val);
	if (rv)
		return rv;
	if (val & RT946X_MASK_CHG_VBATOV)
		*status |= CHARGER_VOLTAGE_OR;


	rv = rt946x_read8(RT946X_REG_CHGNTC, &val);
	if (rv)
		return rv;
	val = (val & RT946X_MASK_BATNTC_FAULT) >> RT946X_SHIFT_BATNTC_FAULT;

	switch (val) {
	case RT946X_BATTEMP_WARM:
		*status |= CHARGER_RES_HOT;
		break;
	case RT946X_BATTEMP_COOL:
		*status |= CHARGER_RES_COLD;
		break;
	case RT946X_BATTEMP_COLD:
		*status |= CHARGER_RES_COLD;
		*status |= CHARGER_RES_UR;
		break;
	case RT946X_BATTEMP_HOT:
		*status |= CHARGER_RES_HOT;
		*status |= CHARGER_RES_OR;
		break;
	default:
		break;
	}

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv;

	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = rt946x_por_reset();
		if (rv)
			return rv;
	}

	if (mode & CHARGE_FLAG_RESET_TO_ZERO) {
		rv = rt946x_reset_to_zero();
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

int charger_get_current(int *current)
{
	int rv;
	int val = 0;
	const struct charger_info * const info = charger_get_info();

	rv = rt946x_read8(RT946X_REG_CHGCTRL7, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_ICHG) >> RT946X_SHIFT_ICHG;
	*current = val * info->current_step + info->current_min;

	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	uint8_t reg_icc = 0;
	const struct charger_info * const info = charger_get_info();

	reg_icc = rt946x_closest_reg(info->current_min, info->current_max,
		info->current_step, current);

	return rt946x_update_bits(RT946X_REG_CHGCTRL7, RT946X_MASK_ICHG,
		reg_icc << RT946X_SHIFT_ICHG);
}

int charger_get_voltage(int *voltage)
{
	int rv;
	int val = 0;
	const struct charger_info * const info = charger_get_info();

	rv = rt946x_read8(RT946X_REG_CHGCTRL4, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_CV) >> RT946X_SHIFT_CV;
	*voltage = val * info->voltage_step + info->voltage_min;

	return EC_SUCCESS;
}

int charger_set_voltage(int voltage)
{
	uint8_t reg_cv = 0;
	const struct charger_info * const info = charger_get_info();

	reg_cv = rt946x_closest_reg(info->voltage_min, info->voltage_max,
		info->voltage_step, voltage);

	return rt946x_update_bits(RT946X_REG_CHGCTRL4, RT946X_MASK_CV,
		reg_cv << RT946X_SHIFT_CV);
}

int charger_discharge_on_ac(int enable)
{
	return rt946x_enable_hz(enable);
}

int charger_get_vbus_voltage(int port)
{
	int val;
	static int vbus_mv;
	int retries = 10;

	/* Set VBUS as ADC input */
	rt946x_update_bits(RT946X_REG_CHGADC, RT946X_MASK_ADC_IN_SEL,
		RT946X_ADC_VBUS_DIV5 << RT946X_SHIFT_ADC_IN_SEL);

	/* Start ADC conversion */
	rt946x_set_bit(RT946X_REG_CHGADC, RT946X_MASK_ADC_START);

	/*
	 * In practice, ADC conversion rarely takes more than 35ms.
	 * However, according to the datasheet, ADC conversion may take
	 * up to 200ms. But we can't wait for that long, otherwise
	 * host command would time out. So here we set ADC timeout as 50ms.
	 * If ADC times out, we just return the last read vbus_mv.
	 *
	 * TODO(chromium:820335): We may handle this more gracefully with
	 * EC_RES_IN_PROGRESS.
	 */
	while (--retries) {
		rt946x_read8(RT946X_REG_CHGSTAT, &val);
		if (!(val & RT946X_MASK_ADC_STAT))
			break;
		msleep(5);
	}

	if (retries) {
		/* Read measured results if ADC finishes in time. */
		rt946x_read8(RT946X_REG_ADCDATAL, &vbus_mv);
		rt946x_read8(RT946X_REG_ADCDATAH, &val);
		vbus_mv |= (val << 8);
		vbus_mv *= 25;
	}

	return vbus_mv;
}

/* Setup sourcing current to prevent overload */
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
static int rt946x_enable_ilim_pin(int en)
{
	int ret;

	ret = (en ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_CHGCTRL3, RT946X_MASK_ILIMEN);

	return ret;
}

static int rt946x_select_ilmt(enum rt946x_ilmtsel sel)
{
	int ret;

	ret = rt946x_update_bits(RT946X_REG_CHGCTRL2, RT946X_MASK_ILMTSEL,
		sel << RT946X_SHIFT_ILMTSEL);

	return ret;
}
#endif /* CONFIG_CHARGER_ILIM_PIN_DISABLED */

/* Charging power state initialization */
int charger_post_init(void)
{
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	int rv;

	rv = rt946x_select_ilmt(RT946X_ILMTSEL_AICR);
	if (rv)
		return rv;
	/* Disable ILIM pin */
	rv = rt946x_enable_ilim_pin(0);
	if (rv)
		return rv;
#endif
	return EC_SUCCESS;
}

/* Hardware current ramping (aka AICL: Average Input Current Level) */
#ifdef CONFIG_CHARGE_RAMP_HW
static int rt946x_get_mivr(int *mivr)
{
	int rv;
	int val = 0;

	rv = rt946x_read8(RT946X_REG_CHGCTRL6, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_MIVR) >> RT946X_SHIFT_MIVR;
	*mivr = val * RT946X_MIVR_STEP + RT946X_MIVR_MIN;

	return EC_SUCCESS;
}

static int rt946x_set_aicl_vth(uint8_t aicl_vth)
{
	uint8_t reg_aicl_vth = 0;

	reg_aicl_vth = rt946x_closest_reg(RT946X_AICLVTH_MIN,
		RT946X_AICLVTH_MAX, RT946X_AICLVTH_STEP, aicl_vth);

	return rt946x_update_bits(RT946X_REG_CHGCTRL14, RT946X_MASK_AICLVTH,
		reg_aicl_vth << RT946X_SHIFT_AICLVTH);
}

int charger_set_hw_ramp(int enable)
{
	int rv;
	unsigned int mivr = 0;

	if (!enable) {
		rv = rt946x_clr_bit(RT946X_REG_CHGCTRL14, RT946X_MASK_AICLMEAS);
		return rv;
	}

	rv = rt946x_get_mivr(&mivr);
	if (rv < 0)
		return rv;

	/*
	 * Check if there's a suitable AICL_VTH.
	 * The vendor suggests setting AICL_VTH as (MIVR + 200mV).
	 */
	if ((mivr + 200) > RT946X_AICLVTH_MAX) {
		CPRINTF("%s: no suitable vth, mivr = %d\n", __func__, mivr);
		return EC_ERROR_INVAL;
	}

	rv = rt946x_set_aicl_vth(mivr + 200);
	if (rv < 0)
		return rv;

	return rt946x_set_bit(RT946X_REG_CHGCTRL14, RT946X_MASK_AICLMEAS);
}

int chg_ramp_is_stable(void)
{
	int rv;
	int val = 0;

	rv = rt946x_read8(RT946X_REG_CHGCTRL14, &val);
	val = (val & RT946X_MASK_AICLMEAS) >> RT946X_SHIFT_AICLMEAS;

	return (!rv && !val);
}

int chg_ramp_is_detected(void)
{
	return 1;
}

int chg_ramp_get_current_limit(void)
{
	int rv;
	int input_current = 0;

	rv = charger_get_input_current(&input_current);

	return rv ? -1 : input_current;
}
#endif /* CONFIG_CHARGE_RAMP_HW */

static void rt946x_init(void)
{
	int reg = 0xFFFFFFFF;

	/* Check device id */
	if (charger_device_id(&reg) || reg != RT946X_VENDOR_ID) {
		CPRINTF("RT946X incorrect ID: 0x%02x\n", reg);
		return;
	}

	/* Check revision id */
	if (rt946x_chip_rev(&reg)) {
		CPRINTF("Failed to read RT946X CHIP REV\n");
		return;
	}
	CPRINTF("RT946X CHIP REV: 0x%02x\n", reg);

	if (rt946x_init_setting()) {
		CPRINTF("RT946X init failed\n");
		return;
	}
	CPRINTF("RT946X init succeeded\n");
}
DECLARE_HOOK(HOOK_INIT, rt946x_init, HOOK_PRIO_INIT_I2C + 1);

#ifdef HAS_TASK_USB_CHG
static int rt946x_get_bc12_device_type(void)
{
	int reg;

#if defined(CONFIG_CHARGER_RT9466) || defined(CONFIG_CHARGER_RT9467)
	if (rt946x_read8(RT946X_REG_DPDM1, &reg))
		return CHARGE_SUPPLIER_NONE;

	switch (reg & RT946X_MASK_BC12_TYPE) {
	case RT946X_MASK_SDP:
		return CHARGE_SUPPLIER_BC12_SDP;
	case RT946X_MASK_CDP:
		return CHARGE_SUPPLIER_BC12_CDP;
	case RT946X_MASK_DCP:
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		return CHARGE_SUPPLIER_NONE;
	}
#elif defined(CONFIG_CHARGER_MT6370)
	if (rt946x_read8(MT6370_REG_USBSTATUS1, &reg))
		return CHARGE_SUPPLIER_NONE;

	switch ((reg & MT6370_MASK_USB_STATUS) >> MT6370_SHIFT_USB_STATUS) {
	case MT6370_CHG_TYPE_SDP:
	case MT6370_CHG_TYPE_SDPNSTD:
		return CHARGE_SUPPLIER_BC12_SDP;
	case MT6370_CHG_TYPE_CDP:
		return CHARGE_SUPPLIER_BC12_CDP;
	case MT6370_CHG_TYPE_DCP:
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		return CHARGE_SUPPLIER_NONE;
	}
#endif
}

static int rt946x_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		if (IS_ENABLED(CONFIG_CHARGE_RAMP_SW) ||
				IS_ENABLED(CONFIG_CHARGE_RAMP_HW))
			/* A conservative value to prevent a bad charger. */
			return 2000;
		/* fallback */
	case CHARGE_SUPPLIER_BC12_CDP:
		return 1500;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

void rt946x_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
}

int rt946x_toggle_bc12_detection(void)
{
	int rv;
	rv = rt946x_enable_bc12_detection(0);
	if (rv)
		return rv;
	/* mt6370 requires 40us delay to toggle RT946X_MASK_USBCHGEN */
	udelay(40);
	return rt946x_enable_bc12_detection(1);
}

#ifdef CONFIG_CHARGER_MT6370_BC12_GPIO
static void usb_pd_connect(void)
{
	rt946x_toggle_bc12_detection();
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, usb_pd_connect, HOOK_PRIO_DEFAULT);
#endif

void usb_charger_task(void *u)
{
	struct charge_port_info chg;
	int bc12_type = CHARGE_SUPPLIER_NONE;
	int reg = 0;

	chg.voltage = USB_CHARGER_VOLTAGE_MV;
	while (1) {
		rt946x_read8(RT946X_REG_DPDMIRQ, &reg);

		/* VBUS attach event */
		if (reg & RT946X_MASK_DPDMIRQ_ATTACH) {
			CPRINTS("VBUS attached: %dmV",
					charger_get_vbus_voltage(0));
			bc12_type = rt946x_get_bc12_device_type();

			CPRINTS("BC12 type %d", bc12_type);
			if (bc12_type != CHARGE_SUPPLIER_NONE) {
#ifdef CONFIG_WIRELESS_CHARGER_P9221_R7
				if ((bc12_type == CHARGE_SUPPLIER_BC12_SDP) &&
						wpc_chip_is_online()) {
					p9221_notify_vbus_change(1);
					CPRINTS("WPC ON");
				} else {

#endif
					chg.current = rt946x_get_bc12_ilim(
								bc12_type);
					charge_manager_update_charge(bc12_type,
								     0, &chg);
#ifdef CONFIG_WIRELESS_CHARGER_P9221_R7
				}
#endif
			}

			rt946x_enable_bc12_detection(0);
			hook_notify(HOOK_AC_CHANGE);
		}

		/* VBUS detach event */
		if (reg & RT946X_MASK_DPDMIRQ_DETACH) {
			CPRINTS("VBUS detached");
#ifdef CONFIG_WIRELESS_CHARGER_P9221_R7
			p9221_notify_vbus_change(0);
#endif
			charge_manager_update_charge(bc12_type, 0, NULL);

			if (!IS_ENABLED(CONFIG_CHARGER_MT6370_BC12_GPIO))
				rt946x_enable_bc12_detection(1);

			hook_notify(HOOK_AC_CHANGE);
		}

		task_wait_event(-1);
	}
}

int usb_charger_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP;
}

int usb_charger_ramp_max(int supplier, int sup_curr)
{
	return rt946x_get_bc12_ilim(supplier);
}
#endif /* HAS_TASK_USB_CHG */

/* Non-standard interface functions */

int rt946x_enable_charger_boost(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_CHGCTRL2, RT946X_MASK_CHG_EN);
}

/*
 * rt946x reports VBUS ready after VBUS is up for ~500ms.
 * Check if this works for the use case before calling this function.
 */
int rt946x_is_vbus_ready(void)
{
	int val = 0;

	return rt946x_read8(RT946X_REG_CHGSTATC, &val) ?
	       0 : !!(val & RT946X_MASK_PWR_RDY);
}

int rt946x_is_charge_done(void)
{
	int val = 0;

	if (rt946x_read8(RT946X_REG_CHGSTAT, &val))
		return 0;

	val = (val & RT946X_MASK_CHG_STAT) >> RT946X_SHIFT_CHG_STAT;

	return val == RT946X_CHGSTAT_DONE;
}

int rt946x_cutoff_battery(void)
{
	int val = RT946X_MASK_SHIP_MODE;

#ifdef CONFIG_CHARGER_MT6370
	val |= RT946X_MASK_TE | RT946X_MASK_CFO_EN | RT946X_MASK_CHG_EN;
#endif
	return rt946x_set_bit(RT946X_REG_CHGCTRL2, val);
}

int rt946x_enable_charge_termination(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)
		(RT946X_REG_CHGCTRL2, RT946X_MASK_TE);
}

#ifdef CONFIG_CHARGER_MT6370
/* MT6370 LDO */

int mt6370_set_ldo_voltage(int mv)
{
	int rv;
	int vout_val;
	const int vout_mask = MT6370_MASK_LDOVOUT_EN | MT6370_MASK_LDOVOUT_VOUT;

	/* LDO output-off mode to floating. */
	rv = rt946x_update_bits(MT6370_REG_LDOCFG, MT6370_MASK_LDOCFG_OMS, 0);
	if (rv)
		return rv;

	/* Disable LDO if voltage is zero. */
	if (mv == 0)
		return rt946x_clr_bit(MT6370_REG_LDOVOUT,
				      MT6370_MASK_LDOVOUT_EN);

	vout_val = 1 << MT6370_SHIFT_LDOVOUT_EN;
	vout_val |= rt946x_closest_reg(MT6370_LDO_MIN, MT6370_LDO_MAX,
				       MT6370_LDO_STEP, mv);
	return rt946x_update_bits(MT6370_REG_LDOVOUT, vout_mask, vout_val);
}

/* MT6370 Display bias */
int mt6370_db_external_control(int en)
{
	return rt946x_update_bits(MT6370_REG_DBCTRL1, MT6370_MASK_DB_EXT_EN,
				  en << MT6370_SHIFT_DB_EXT_EN);
}

int mt6370_db_set_voltages(int vbst, int vpos, int vneg)
{
	int rv;

	/* set display bias VBST */
	rv = rt946x_update_bits(MT6370_REG_DBVBST, MT6370_MASK_DB_VBST,
				rt946x_closest_reg(MT6370_DB_VBST_MIN,
						   MT6370_DB_VBST_MAX,
						   MT6370_DB_VBST_STEP, vbst));

	/* set display bias VPOS */
	rv |= rt946x_update_bits(MT6370_REG_DBVPOS, MT6370_MASK_DB_VPOS,
				 rt946x_closest_reg(MT6370_DB_VPOS_MIN,
						    MT6370_DB_VPOS_MAX,
						    MT6370_DB_VPOS_STEP, vpos));

	/* set display bias VNEG */
	rv |= rt946x_update_bits(MT6370_REG_DBVNEG, MT6370_MASK_DB_VNEG,
				 rt946x_closest_reg(MT6370_DB_VNEG_MIN,
						    MT6370_DB_VNEG_MAX,
						    MT6370_DB_VNEG_STEP, vneg));

	/* Enable VNEG/VPOS discharge when VNEG/VPOS rails disabled. */
	rv |= rt946x_update_bits(
		MT6370_REG_DBCTRL2,
		MT6370_MASK_DB_VNEG_DISC | MT6370_MASK_DB_VPOS_DISC,
		MT6370_MASK_DB_VNEG_DISC | MT6370_MASK_DB_VPOS_DISC);

	return rv;
}

/* MT6370 BACKLIGHT LED */

int mt6370_backlight_set_dim(uint16_t dim)
{
	int rv;

	/* datasheet suggests that update BLDIM2 first then BLDIM */
	rv = rt946x_write8(MT6370_BACKLIGHT_BLDIM2, dim & MT6370_MASK_BLDIM2);

	if (rv)
		return rv;

	rv = rt946x_write8(MT6370_BACKLIGHT_BLDIM,
			   (dim >> MT6370_SHIFT_BLDIM_MSB) & MT6370_MASK_BLDIM);

	return rv;
}

/* MT6370 RGB LED */

int mt6370_led_set_dim_mode(enum mt6370_led_index index,
			    enum mt6370_led_dim_mode mode)
{
	if (index <= MT6370_LED_ID_OFF || index >= MT6370_LED_ID_COUNT)
		return EC_ERROR_INVAL;

	rt946x_update_bits(MT6370_REG_RGBDIM_BASE + index,
			   MT6370_MASK_RGB_DIMMODE,
			   mode << MT6370_SHIFT_RGB_DIMMODE);
	return EC_SUCCESS;
}

int mt6370_led_set_color(uint8_t mask)
{
	return rt946x_update_bits(MT6370_REG_RGBEN, MT6370_MASK_RGB_ISNK_ALL_EN,
				  mask);
}

int mt6370_led_set_brightness(enum mt6370_led_index index, uint8_t brightness)
{
	if (index >= MT6370_LED_ID_COUNT || index <= MT6370_LED_ID_OFF)
		return EC_ERROR_INVAL;

	rt946x_update_bits(MT6370_REG_RGBISNK_BASE + index,
			   MT6370_MASK_RGBISNK_CURSEL,
			   brightness << MT6370_SHIFT_RGBISNK_CURSEL);
	return EC_SUCCESS;
}

int mt6370_led_set_pwm_dim_duty(enum mt6370_led_index index, uint8_t dim_duty)
{
	if (index >= MT6370_LED_ID_COUNT || index <= MT6370_LED_ID_OFF)
		return EC_ERROR_INVAL;

	rt946x_update_bits(MT6370_REG_RGBDIM_BASE + index,
			   MT6370_MASK_RGB_DIMDUTY,
			   dim_duty << MT6370_SHIFT_RGB_DIMDUTY);
	return EC_SUCCESS;
}

int mt6370_led_set_pwm_frequency(enum mt6370_led_index index,
				 enum mt6370_led_pwm_freq freq)
{
	if (index >= MT6370_LED_ID_COUNT || index <= MT6370_LED_ID_OFF)
		return EC_ERROR_INVAL;

	rt946x_update_bits(MT6370_REG_RGBISNK_BASE + index,
			   MT6370_MASK_RGBISNK_DIMFSEL,
			   freq << MT6370_SHIFT_RGBISNK_DIMFSEL);
	return EC_SUCCESS;
}
#endif /* CONFIG_CHARGER_MT6370 */
