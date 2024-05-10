/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Richtek rt946x, Mediatek mt6370 battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charger.h"
#include "common.h"
#include "compile_time_macros.h"
#include "config.h"
#include "console.h"
#include "driver/wpc/p9221.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "printf.h"
#include "rt946x.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) \
	cprints(CC_CHARGER, "%s " format, "RT946X", ##args)

/* Charger parameters */
#define CHARGER_NAME RT946X_CHARGER_NAME
#define CHARGE_V_MAX 4710
#define CHARGE_V_MIN 3900
#define CHARGE_V_STEP 10
#define CHARGE_I_MAX 5000
#define CHARGE_I_MIN 100
#define CHARGE_I_OFF 0
#define CHARGE_I_STEP 100
#define INPUT_I_MAX 3250
#define INPUT_I_MIN 100
#define INPUT_I_STEP 50

/* Charger parameters */
static const struct charger_info rt946x_charger_info = {
	.name = CHARGER_NAME,
	.voltage_max = CHARGE_V_MAX,
	.voltage_min = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max = CHARGE_I_MAX,
	.current_min = CHARGE_I_MIN,
	.current_step = CHARGE_I_STEP,
	.input_current_max = INPUT_I_MAX,
	.input_current_min = INPUT_I_MIN,
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

__attribute__((weak)) const struct rt946x_init_setting *
board_rt946x_init_setting(void)
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

static struct mutex adc_access_lock;

#ifdef CONFIG_CHARGER_MT6370
/*
 * Unit for each ADC parameter
 * 0 stands for reserved
 */
static const int mt6370_adc_unit[MT6370_ADC_MAX] = {
	0,
	MT6370_ADC_UNIT_VBUS_DIV5,
	MT6370_ADC_UNIT_VBUS_DIV2,
	MT6370_ADC_UNIT_VSYS,
	MT6370_ADC_UNIT_VBAT,
	0,
	MT6370_ADC_UNIT_TS_BAT,
	0,
	MT6370_ADC_UNIT_IBUS,
	MT6370_ADC_UNIT_IBAT,
	0,
	MT6370_ADC_UNIT_CHG_VDDP,
	MT6370_ADC_UNIT_TEMP_JC,
};

static const int mt6370_adc_offset[MT6370_ADC_MAX] = {
	0,
	MT6370_ADC_OFFSET_VBUS_DIV5,
	MT6370_ADC_OFFSET_VBUS_DIV2,
	MT6370_ADC_OFFSET_VSYS,
	MT6370_ADC_OFFSET_VBAT,
	0,
	MT6370_ADC_OFFSET_TS_BAT,
	0,
	MT6370_ADC_OFFSET_IBUS,
	MT6370_ADC_OFFSET_IBAT,
	0,
	MT6370_ADC_OFFSET_CHG_VDDP,
	MT6370_ADC_OFFSET_TEMP_JC,
};

static int hidden_mode_cnt = 0;
static struct mutex hidden_mode_lock;
static const unsigned char mt6370_reg_en_hidden_mode[] = {
	MT6370_REG_HIDDENPASCODE1,
	MT6370_REG_HIDDENPASCODE2,
	MT6370_REG_HIDDENPASCODE3,
	MT6370_REG_HIDDENPASCODE4,
};

static const unsigned char mt6370_val_en_hidden_mode[] = {
	0x96,
	0x69,
	0xC3,
	0x3C,
};

static const unsigned char mt6370_val_en_test_mode[] = {
	0x69,
	0x96,
	0x63,
	0x70,
};
#endif /* CONFIG_CHARGER_MT6370 */

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
	0xBF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static const uint8_t rt946x_irq_maskall[RT946X_IRQ_COUNT] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
#endif

static enum ec_error_list rt946x_set_current(int chgnum, int current);
static enum ec_error_list rt946x_get_current(int chgnum, int *current);
static enum ec_error_list rt946x_set_voltage(int chgnum, int voltage);
static enum ec_error_list rt946x_enable_otg_power(int chgnum, int enabled);
static const struct charger_info *rt946x_get_info(int chgnum);

/* Must be in ascending order */
static const uint16_t rt946x_boost_current[] = {
	500, 700, 1100, 1300, 1800, 2100, 2400,
};

static enum ec_error_list rt946x_read8(int chgnum, int reg, int *val)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			 chg_chips[chgnum].i2c_addr_flags, reg, val);
}

static enum ec_error_list rt946x_write8(int chgnum, int reg, int val)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags, reg, val);
}

static enum ec_error_list rt946x_block_write(int chgnum, int reg,
					     const uint8_t *val, int len)
{
	return i2c_write_block(chg_chips[chgnum].i2c_port,
			       chg_chips[chgnum].i2c_addr_flags, reg, val, len);
}

static int rt946x_update_bits(int chgnum, int reg, int mask, int val)
{
	int rv;
	int reg_val = 0;

	rv = rt946x_read8(chgnum, reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = rt946x_write8(chgnum, reg, reg_val);
	return rv;
}

static inline int rt946x_set_bit(int chgnum, int reg, int mask)
{
	return rt946x_update_bits(chgnum, reg, mask, mask);
}

static inline int rt946x_clr_bit(int chgnum, int reg, int mask)
{
	return rt946x_update_bits(chgnum, reg, mask, 0x00);
}

static inline int mt6370_pmu_reg_test_bit(int chgnum, int cmd, int shift,
					  int *is_one)
{
	int rv, data;

	rv = rt946x_read8(chgnum, cmd, &data);
	if (rv) {
		*is_one = 0;
		return rv;
	}

	*is_one = !!(data & BIT(shift));
	return rv;
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

static int rt946x_get_ieoc(int chgnum, uint32_t *ieoc)
{
	int ret, reg_ieoc;

	ret = rt946x_read8(chgnum, RT946X_REG_CHGCTRL9, &reg_ieoc);
	if (ret)
		return ret;

	*ieoc = RT946X_IEOC_MIN +
		RT946X_IEOC_STEP *
			((reg_ieoc & RT946X_MASK_IEOC) >> RT946X_SHIFT_IEOC);

	return EC_SUCCESS;
}

#ifdef CONFIG_CHARGER_MT6370
static int mt6370_enable_hidden_mode(int chgnum, int en)
{
	int rv = 0;

	if (in_interrupt_context()) {
		CPRINTS("Err: use hidden mode in IRQ");
		return EC_ERROR_INVAL;
	}

	mutex_lock(&hidden_mode_lock);
	if (en) {
		if (hidden_mode_cnt == 0) {
			rv = rt946x_block_write(
				chgnum, mt6370_reg_en_hidden_mode[0],
				mt6370_val_en_hidden_mode,
				ARRAY_SIZE(mt6370_val_en_hidden_mode));
			if (rv)
				goto out;
		}
		hidden_mode_cnt++;
	} else {
		if (hidden_mode_cnt == 1) /* last one */
			rv = rt946x_write8(chgnum, mt6370_reg_en_hidden_mode[0],
					   0x00);
		hidden_mode_cnt--;
		if (rv)
			goto out;
	}

out:
	mutex_unlock(&hidden_mode_lock);
	return rv;
}

/*
 * Vsys short protection:
 * When the system is charging at 500mA, and if Isys > 3600mA, the
 * power path will be turned off and cause the system shutdown.
 * When Ichg < 400mA, then power path is roughly 1/8 of the original.
 * When Isys > 3600mA, this cause the voltage between Vbat and Vsys too
 * huge (Vbat - Vsys > Vsys short portection) and turns off the power
 * path.
 * To workaround this,
 * 1. disable Vsys short protection when Ichg is set below 900mA
 * 2. forbids Ichg <= 400mA (this is done natually on mt6370, since mt6370's
 *    minimum current is 512)
 */
static int mt6370_ichg_workaround(int chgnum, int new_ichg)
{
	int rv = EC_SUCCESS;
	int curr_ichg;

	/*
	 * TODO(b:144532905): The workaround should be applied to rt9466 as
	 * well. But this needs rt9466's hidden register datasheet. Enable
	 * this if we need it in the future.
	 */
	if (!IS_ENABLED(CONFIG_CHARGER_MT6370))
		return EC_SUCCESS;

	rv = rt946x_get_current(chgnum, &curr_ichg);
	if (rv)
		return rv;

	mt6370_enable_hidden_mode(chgnum, 1);

	/* disable Vsys protect if if the new ichg is below 900mA */
	if (curr_ichg >= 900 && new_ichg < 900)
		rv = rt946x_update_bits(chgnum, RT946X_REG_CHGHIDDENCTRL7,
					RT946X_MASK_HIDDENCTRL7_VSYS_PROTECT,
					0);
	/* enable Vsys protect if the new ichg is above 900mA */
	else if (new_ichg >= 900 && curr_ichg < 900)
		rv = rt946x_update_bits(chgnum, RT946X_REG_CHGHIDDENCTRL7,
					RT946X_MASK_HIDDENCTRL7_VSYS_PROTECT,
					RT946X_ENABLE_VSYS_PROTECT);

	mt6370_enable_hidden_mode(chgnum, 0);
	return rv;
}
#endif /* CONFIG_CHARGER_MT6370 */

static inline int rt946x_enable_wdt(int chgnum, int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)(
		chgnum, RT946X_REG_CHGCTRL13, RT946X_MASK_WDT_EN);
}

/* Enable high-impedance mode */
static inline int rt946x_enable_hz(int chgnum, int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)(
		chgnum, RT946X_REG_CHGCTRL1, RT946X_MASK_HZ_EN);
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
	rv = rt946x_write8(CHARGER_SOLO, MT6370_REG_RSTPASCODE1,
			   MT6370_MASK_RSTPASCODE1);
	rv |= rt946x_write8(CHARGER_SOLO, MT6370_REG_RSTPASCODE2,
			    MT6370_MASK_RSTPASCODE2);
#else
	/* Hard reset, may take several milliseconds. */
	val = RT946X_MASK_RST;
	rv = rt946x_enable_hz(CHARGER_SOLO, 0);
#endif
	if (rv)
		return rv;

	return rt946x_set_bit(CHARGER_SOLO, RT946X_REG_CORECTRL_RST, val);
}

static int rt946x_reset_to_zero(int chgnum)
{
	int rv;

	rv = rt946x_set_current(chgnum, 0);
	if (rv)
		return rv;

	rv = rt946x_set_voltage(chgnum, 0);
	if (rv)
		return rv;

	return rt946x_enable_hz(chgnum, 1);
}

static int rt946x_enable_bc12_detection(int chgnum, int en)
{
#if defined(CONFIG_CHARGER_RT9467) || defined(CONFIG_CHARGER_MT6370)
	int rv;

	if (en) {
#ifdef CONFIG_CHARGER_MT6370_BC12_GPIO
		gpio_set_level(GPIO_BC12_DET_EN, 1);
#endif /* CONFIG_CHARGER_MT6370_BC12_GPIO */
		return rt946x_set_bit(chgnum, RT946X_REG_DPDM1,
				      RT946X_MASK_USBCHGEN);
	}

	rv = rt946x_clr_bit(chgnum, RT946X_REG_DPDM1, RT946X_MASK_USBCHGEN);
#ifdef CONFIG_CHARGER_MT6370_BC12_GPIO
	gpio_set_level(GPIO_BC12_DET_EN, 0);
#endif /* CONFIG_CHARGER_MT6370_BC12_GPIO */
	return rv;
#endif
	return 0;
}

static int rt946x_set_ieoc(int chgnum, unsigned int ieoc)
{
	uint8_t reg_ieoc;

	reg_ieoc = rt946x_closest_reg(RT946X_IEOC_MIN, RT946X_IEOC_MAX,
				      RT946X_IEOC_STEP, ieoc);

	CPRINTS("ieoc=%d", ieoc);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL9, RT946X_MASK_IEOC,
				  reg_ieoc << RT946X_SHIFT_IEOC);
}

static int rt946x_set_mivr(int chgnum, unsigned int mivr)
{
	uint8_t reg_mivr = 0;

	reg_mivr = rt946x_closest_reg(RT946X_MIVR_MIN, RT946X_MIVR_MAX,
				      RT946X_MIVR_STEP, mivr);

	CPRINTS("mivr=%d", mivr);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL6, RT946X_MASK_MIVR,
				  reg_mivr << RT946X_SHIFT_MIVR);
}

static int rt946x_set_boost_voltage(int chgnum, unsigned int voltage)
{
	uint8_t reg_voltage = 0;

	reg_voltage = rt946x_closest_reg(RT946X_BOOST_VOLTAGE_MIN,
					 RT946X_BOOST_VOLTAGE_MAX,
					 RT946X_BOOST_VOLTAGE_STEP, voltage);

	CPRINTS("voltage=%d", voltage);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL5,
				  RT946X_MASK_BOOST_VOLTAGE,
				  reg_voltage << RT946X_SHIFT_BOOST_VOLTAGE);
}

static int rt946x_set_boost_current(int chgnum, unsigned int current)
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

	CPRINTS("current=%d", current);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL10,
				  RT946X_MASK_BOOST_CURRENT,
				  i << RT946X_SHIFT_BOOST_CURRENT);
}

static int rt946x_set_ircmp_vclamp(int chgnum, unsigned int vclamp)
{
	uint8_t reg_vclamp = 0;

	reg_vclamp = rt946x_closest_reg(RT946X_IRCMP_VCLAMP_MIN,
					RT946X_IRCMP_VCLAMP_MAX,
					RT946X_IRCMP_VCLAMP_STEP, vclamp);

	CPRINTS("vclamp=%d", vclamp);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL18,
				  RT946X_MASK_IRCMP_VCLAMP,
				  reg_vclamp << RT946X_SHIFT_IRCMP_VCLAMP);
}

static int rt946x_set_ircmp_res(int chgnum, unsigned int res)
{
	uint8_t reg_res = 0;

	reg_res = rt946x_closest_reg(RT946X_IRCMP_RES_MIN, RT946X_IRCMP_RES_MAX,
				     RT946X_IRCMP_RES_STEP, res);

	CPRINTS("res=%d", res);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL18,
				  RT946X_MASK_IRCMP_RES,
				  reg_res << RT946X_SHIFT_IRCMP_RES);
}

static int rt946x_set_vprec(int chgnum, unsigned int vprec)
{
	uint8_t reg_vprec = 0;

	reg_vprec = rt946x_closest_reg(RT946X_VPREC_MIN, RT946X_VPREC_MAX,
				       RT946X_VPREC_STEP, vprec);

	CPRINTS("vprec=%d", vprec);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL8,
				  RT946X_MASK_VPREC,
				  reg_vprec << RT946X_SHIFT_VPREC);
}

static int rt946x_set_iprec(int chgnum, unsigned int iprec)
{
	uint8_t reg_iprec = 0;

	reg_iprec = rt946x_closest_reg(RT946X_IPREC_MIN, RT946X_IPREC_MAX,
				       RT946X_IPREC_STEP, iprec);

	CPRINTS("iprec=%d", iprec);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL8,
				  RT946X_MASK_IPREC,
				  reg_iprec << RT946X_SHIFT_IPREC);
}

static int rt946x_init_irq(int chgnum)
{
	int rv = 0;
	int unused;
	int i;

	/* Mask all interrupts */
	rv = rt946x_block_write(chgnum, RT946X_REG_CHGSTATCCTRL,
				rt946x_irq_maskall, RT946X_IRQ_COUNT);
	if (rv)
		return rv;

	/* Clear all interrupt flags */
	for (i = 0; i < RT946X_IRQ_COUNT; i++) {
		rv = rt946x_read8(chgnum, RT946X_REG_CHGSTATC + i, &unused);
		if (rv)
			return rv;
	}

	/* Init interrupt */
	return rt946x_block_write(chgnum, RT946X_REG_CHGSTATCCTRL,
				  rt946x_irqmask, ARRAY_SIZE(rt946x_irqmask));
}

static int rt946x_init_setting(int chgnum)
{
	int rv = 0;
	const struct battery_info *batt_info = battery_get_info();
	const struct rt946x_init_setting *setting = board_rt946x_init_setting();

#ifdef CONFIG_BATTERY_SMART
	/* Disable EOC */
	rv = rt946x_enable_charge_eoc(0);
	if (rv)
		return rv;
#endif

#ifdef CONFIG_CHARGER_OTG
	/*  Disable boost-mode output voltage */
	rv = rt946x_enable_otg_power(chgnum, 0);
	if (rv)
		return rv;
#endif
	/* Disable BC 1.2 detection by default. It will be enabled on demand */
	rv = rt946x_enable_bc12_detection(chgnum, 0);
	if (rv)
		return rv;
	/* Disable WDT */
	rv = rt946x_enable_wdt(chgnum, 0);
	if (rv)
		return rv;
	/* Disable battery thermal protection */
	rv = rt946x_clr_bit(chgnum, RT946X_REG_CHGCTRL16, RT946X_MASK_JEITA_EN);
	if (rv)
		return rv;
	/* Disable charge timer */
	rv = rt946x_clr_bit(chgnum, RT946X_REG_CHGCTRL12, RT946X_MASK_TMR_EN);
	if (rv)
		return rv;
	rv = rt946x_set_mivr(chgnum, setting->mivr);
	if (rv)
		return rv;
	rv = rt946x_set_ieoc(chgnum, setting->eoc_current);
	if (rv)
		return rv;
	rv = rt946x_set_boost_voltage(chgnum, setting->boost_voltage);
	if (rv)
		return rv;
	rv = rt946x_set_boost_current(chgnum, setting->boost_current);
	if (rv)
		return rv;
	rv = rt946x_set_ircmp_vclamp(chgnum, setting->ircmp_vclamp);
	if (rv)
		return rv;
	rv = rt946x_set_ircmp_res(chgnum, setting->ircmp_res);
	if (rv)
		return rv;
	rv = rt946x_set_vprec(chgnum, batt_info->precharge_voltage ?
					      batt_info->precharge_voltage :
					      batt_info->voltage_min);
	if (rv)
		return rv;
	rv = rt946x_set_iprec(chgnum, batt_info->precharge_current);
	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_MT6370_BACKLIGHT
	rt946x_write8(
		chgnum, MT6370_BACKLIGHT_BLEN,
		MT6370_MASK_BLED_EXT_EN | MT6370_MASK_BLED_EN |
			MT6370_MASK_BLED_1CH_EN | MT6370_MASK_BLED_2CH_EN |
			MT6370_MASK_BLED_3CH_EN | MT6370_MASK_BLED_4CH_EN |
			MT6370_BLED_CODE_LINEAR);
	rt946x_update_bits(chgnum, MT6370_BACKLIGHT_BLPWM,
			   MT6370_MASK_BLPWM_BLED_PWM,
			   BIT(MT6370_SHIFT_BLPWM_BLED_PWM));
#endif

	return rt946x_init_irq(chgnum);
}

#ifdef CONFIG_CHARGER_OTG
static enum ec_error_list rt946x_enable_otg_power(int chgnum, int enabled)
{
	return (enabled ? rt946x_set_bit : rt946x_clr_bit)(
		chgnum, RT946X_REG_CHGCTRL1, RT946X_MASK_OPA_MODE);
}

static int rt946x_is_sourcing_otg_power(int chgnum, int port)
{
	int val;

	if (rt946x_read8(CHARGER_SOLO, RT946X_REG_CHGCTRL1, &val))
		return 0;

	return !!(val & RT946X_MASK_OPA_MODE);
}
#endif

static enum ec_error_list rt946x_set_input_current_limit(int chgnum,
							 int input_current)
{
	uint8_t reg_iin = 0;
	const struct charger_info *const info = rt946x_get_info(chgnum);

	reg_iin = rt946x_closest_reg(info->input_current_min,
				     info->input_current_max,
				     info->input_current_step, input_current);

	CPRINTS("iin=%d", input_current);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL3, RT946X_MASK_AICR,
				  reg_iin << RT946X_SHIFT_AICR);
}

static enum ec_error_list rt946x_get_input_current_limit(int chgnum,
							 int *input_current)
{
	int rv;
	int val = 0;
	const struct charger_info *const info = rt946x_get_info(chgnum);

	rv = rt946x_read8(chgnum, RT946X_REG_CHGCTRL3, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_AICR) >> RT946X_SHIFT_AICR;
	*input_current =
		val * info->input_current_step + info->input_current_min;

	return EC_SUCCESS;
}

static enum ec_error_list rt946x_manufacturer_id(int chgnum, int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

static enum ec_error_list rt946x_device_id(int chgnum, int *id)
{
	int rv;

	rv = rt946x_read8(chgnum, RT946X_REG_DEVICEID, id);
	if (rv == EC_SUCCESS)
		*id &= RT946X_MASK_VENDOR_ID;
	return rv;
}

static enum ec_error_list rt946x_get_option(int chgnum, int *option)
{
	/* Ignored: does not exist */
	*option = 0;
	return EC_SUCCESS;
}

static enum ec_error_list rt946x_set_option(int chgnum, int option)
{
	/* Ignored: does not exist */
	return EC_SUCCESS;
}

static const struct charger_info *rt946x_get_info(int chgnum)
{
	return &rt946x_charger_info;
}

static enum ec_error_list rt946x_get_status(int chgnum, int *status)
{
	int rv;
	int val = 0;

	rv = rt946x_read8(chgnum, RT946X_REG_CHGCTRL2, &val);
	if (rv)
		return rv;
	val = (val & RT946X_MASK_CHG_EN) >> RT946X_SHIFT_CHG_EN;
	if (!val)
		*status |= CHARGER_CHARGE_INHIBITED;

	rv = rt946x_read8(chgnum, RT946X_REG_CHGFAULT, &val);
	if (rv)
		return rv;
	if (val & RT946X_MASK_CHG_VBATOV)
		*status |= CHARGER_VOLTAGE_OR;

	rv = rt946x_read8(chgnum, RT946X_REG_CHGNTC, &val);
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

static enum ec_error_list rt946x_set_mode(int chgnum, int mode)
{
	int rv;

	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = rt946x_por_reset();
		if (rv)
			return rv;
	}

	if (mode & CHARGE_FLAG_RESET_TO_ZERO) {
		rv = rt946x_reset_to_zero(chgnum);
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

static enum ec_error_list rt946x_get_current(int chgnum, int *current)
{
	int rv;
	int val = 0;
	const struct charger_info *const info = rt946x_get_info(chgnum);

	rv = rt946x_read8(chgnum, RT946X_REG_CHGCTRL7, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_ICHG) >> RT946X_SHIFT_ICHG;
	*current = val * info->current_step + info->current_min;

	return EC_SUCCESS;
}

static enum ec_error_list rt946x_set_current(int chgnum, int current)
{
	int rv;
	uint8_t reg_icc;
	static int workaround;
	const struct charger_info *const info = rt946x_get_info(chgnum);

	/*
	 * mt6370's minimum regulated current is 500mA REG17[7:2] 0b100,
	 * values below 0b100 are preserved.
	 */
	if (IS_ENABLED(CONFIG_CHARGER_MT6370))
		current = MAX(500, current);

#ifdef CONFIG_CHARGER_MT6370
	rv = mt6370_ichg_workaround(chgnum, current);
	if (rv)
		return rv;
#endif

	reg_icc = rt946x_closest_reg(info->current_min, info->current_max,
				     info->current_step, current);

	rv = rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL7, RT946X_MASK_ICHG,
				reg_icc << RT946X_SHIFT_ICHG);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_CHARGER_RT9466) ||
	    IS_ENABLED(CONFIG_CHARGER_MT6370)) {
		uint32_t curr_ieoc;

		/*
		 * workaround to make IEOC accurate:
		 * witht normal charging (ICC >= 900mA), the power path is fully
		 * turned on. But at low charging current state (ICC < 900mA),
		 * the power path will only be partially turned on. So under
		 * such situation, the IEOC is inaccurate.
		 */
		rv = rt946x_get_ieoc(chgnum, &curr_ieoc);
		if (rv)
			return rv;

		if (current < 900 && !workaround) {
			/* raise IEOC if charge current is under 900 */
			rv = rt946x_set_ieoc(chgnum, curr_ieoc + 100);
			workaround = 1;
		} else if (current >= 900 && workaround) {
			/* reset IEOC if charge current is above 900 */
			workaround = 0;
			rv = rt946x_set_ieoc(chgnum, curr_ieoc - 100);
		}
	}

	return rv;
}

static enum ec_error_list rt946x_get_voltage(int chgnum, int *voltage)
{
	int rv;
	int val = 0;
	const struct charger_info *const info = rt946x_get_info(chgnum);

	rv = rt946x_read8(chgnum, RT946X_REG_CHGCTRL4, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_CV) >> RT946X_SHIFT_CV;
	*voltage = val * info->voltage_step + info->voltage_min;

	return EC_SUCCESS;
}

static enum ec_error_list rt946x_set_voltage(int chgnum, int voltage)
{
	uint8_t reg_cv = 0;
	const struct charger_info *const info = rt946x_get_info(chgnum);

	reg_cv = rt946x_closest_reg(info->voltage_min, info->voltage_max,
				    info->voltage_step, voltage);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL4, RT946X_MASK_CV,
				  reg_cv << RT946X_SHIFT_CV);
}

static enum ec_error_list rt946x_discharge_on_ac(int chgnum, int enable)
{
	return rt946x_enable_hz(chgnum, enable);
}

/* Setup sourcing current to prevent overload */
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
static int rt946x_enable_ilim_pin(int chgnum, int en)
{
	int ret;

	ret = (en ? rt946x_set_bit : rt946x_clr_bit)(
		chgnum, RT946X_REG_CHGCTRL3, RT946X_MASK_ILIMEN);

	return ret;
}

static int rt946x_select_ilmt(int chgnum, enum rt946x_ilmtsel sel)
{
	int ret;

	ret = rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL2,
				 RT946X_MASK_ILMTSEL,
				 sel << RT946X_SHIFT_ILMTSEL);

	return ret;
}
#endif /* CONFIG_CHARGER_ILIM_PIN_DISABLED */

/* Charging power state initialization */
static enum ec_error_list rt946x_post_init(int chgnum)
{
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	int rv;

	rv = rt946x_select_ilmt(chgnum, RT946X_ILMTSEL_AICR);
	if (rv)
		return rv;

	/* Need 5ms to ramp after choose current limit source */
	crec_msleep(5);

	/* Disable ILIM pin */
	rv = rt946x_enable_ilim_pin(chgnum, 0);
	if (rv)
		return rv;
#endif
	return EC_SUCCESS;
}

/* Hardware current ramping (aka AICL: Average Input Current Level) */
#ifdef CONFIG_CHARGE_RAMP_HW
static int rt946x_get_mivr(int chgnum, int *mivr)
{
	int rv;
	int val = 0;

	rv = rt946x_read8(chgnum, RT946X_REG_CHGCTRL6, &val);
	if (rv)
		return rv;

	val = (val & RT946X_MASK_MIVR) >> RT946X_SHIFT_MIVR;
	*mivr = val * RT946X_MIVR_STEP + RT946X_MIVR_MIN;

	return EC_SUCCESS;
}

static int rt946x_set_aicl_vth(int chgnum, uint8_t aicl_vth)
{
	uint8_t reg_aicl_vth = 0;

	reg_aicl_vth = rt946x_closest_reg(RT946X_AICLVTH_MIN,
					  RT946X_AICLVTH_MAX,
					  RT946X_AICLVTH_STEP, aicl_vth);

	return rt946x_update_bits(chgnum, RT946X_REG_CHGCTRL14,
				  RT946X_MASK_AICLVTH,
				  reg_aicl_vth << RT946X_SHIFT_AICLVTH);
}

static enum ec_error_list rt946x_set_hw_ramp(int chgnum, int enable)
{
	int rv;
	unsigned int mivr = 0;

	if (!enable) {
		rv = rt946x_clr_bit(chgnum, RT946X_REG_CHGCTRL14,
				    RT946X_MASK_AICLMEAS);
		return rv;
	}

	rv = rt946x_get_mivr(chgnum, &mivr);
	if (rv < 0)
		return rv;

	/*
	 * Check if there's a suitable AICL_VTH.
	 * The vendor suggests setting AICL_VTH as (MIVR + 200mV).
	 */
	if ((mivr + 200) > RT946X_AICLVTH_MAX) {
		CPRINTS("mivr(%d) too high", mivr);
		return EC_ERROR_INVAL;
	}

	rv = rt946x_set_aicl_vth(chgnum, mivr + 200);
	if (rv < 0)
		return rv;

	return rt946x_set_bit(chgnum, RT946X_REG_CHGCTRL14,
			      RT946X_MASK_AICLMEAS);
}

static int rt946x_ramp_is_stable(int chgnum)
{
	int rv;
	int val = 0;

	rv = rt946x_read8(chgnum, RT946X_REG_CHGCTRL14, &val);
	val = (val & RT946X_MASK_AICLMEAS) >> RT946X_SHIFT_AICLMEAS;

	return (!rv && !val);
}

static int rt946x_ramp_is_detected(int chgnum)
{
	return 1;
}

static int rt946x_ramp_get_current_limit(int chgnum)
{
	int rv;
	int input_current = 0;

	rv = rt946x_get_input_current_limit(chgnum, &input_current);

	return rv ? -1 : input_current;
}
#endif /* CONFIG_CHARGE_RAMP_HW */

static void rt946x_init(int chgnum)
{
	int ret = rt946x_init_setting(chgnum);

	CPRINTS("init%d %s(%d)", chgnum, ret ? "fail" : "good", ret);
}

#ifdef HAS_TASK_USB_CHG
#ifdef CONFIG_CHARGER_MT6370
static int mt6370_detect_apple_samsung_ta(int chgnum, int usb_stat)
{
	int ret, reg;
	int chg_type = (usb_stat & MT6370_MASK_USB_STATUS) >>
		       MT6370_SHIFT_USB_STATUS;
	int dp_2_3v, dm_2_3v;

	/* Only SDP/CDP/DCP could possibly be Apple/Samsung TA */
	if (chg_type != MT6370_CHG_TYPE_SDPNSTD &&
	    chg_type != MT6370_CHG_TYPE_CDP && chg_type != MT6370_CHG_TYPE_DCP)
		return chg_type;

	if (chg_type == MT6370_CHG_TYPE_SDPNSTD ||
	    chg_type == MT6370_CHG_TYPE_CDP)
		if (!(usb_stat & MT6370_MASK_DCD_TIMEOUT))
			return chg_type;

	/* Check D+ > 0.9V */
	ret = rt946x_update_bits(chgnum, MT6370_REG_QCSTATUS2,
				 MT6360_MASK_CHECK_DPDM,
				 MT6370_MASK_APP_SS_EN | MT6370_MASK_APP_SS_PL);
	ret |= rt946x_read8(chgnum, MT6370_REG_QCSTATUS2, &reg);

	if (ret)
		return chg_type;

	/* Normal port (D+ < 0.9V) */
	if (!(reg & MT6370_MASK_SS_OUT))
		return chg_type;

	/* Samsung charger (D+ < 1.5V) */
	if (!(reg & MT6370_MASK_APP_OUT))
		return MT6370_CHG_TYPE_SAMSUNG_CHARGER;

	/* Check D+ > 2.3 V */
	ret = rt946x_update_bits(chgnum, MT6370_REG_QCSTATUS2,
				 MT6360_MASK_CHECK_DPDM,
				 MT6370_MASK_APP_REF | MT6370_MASK_APP_SS_PL |
					 MT6370_MASK_APP_SS_EN);
	ret |= rt946x_read8(chgnum, MT6370_REG_QCSTATUS2, &reg);
	dp_2_3v = reg & MT6370_MASK_APP_OUT;

	/* Check D- > 2.3 V */
	ret |= rt946x_update_bits(
		chgnum, MT6370_REG_QCSTATUS2, MT6360_MASK_CHECK_DPDM,
		MT6370_MASK_APP_REF | MT6370_MASK_APP_DPDM_IN |
			MT6370_MASK_APP_SS_PL | MT6370_MASK_APP_SS_EN);
	ret |= rt946x_read8(chgnum, MT6370_REG_QCSTATUS2, &reg);
	dm_2_3v = reg & MT6370_MASK_APP_OUT;

	if (ret)
		return chg_type;

	/* Apple charger */
	if (!dp_2_3v && !dm_2_3v)
		/* Apple 2.5W charger */
		return MT6370_CHG_TYPE_APPLE_0_5A_CHARGER;
	else if (!dp_2_3v && dm_2_3v)
		/* Apple 5W charger */
		return MT6370_CHG_TYPE_APPLE_1_0A_CHARGER;
	else if (dp_2_3v && !dm_2_3v)
		/* Apple 10W charger */
		return MT6370_CHG_TYPE_APPLE_2_1A_CHARGER;
	else
		/* Apple 12W charger */
		return MT6370_CHG_TYPE_APPLE_2_4A_CHARGER;
}
#endif

static int mt6370_get_bc12_device_type(int charger_type)
{
	switch (charger_type) {
	case MT6370_CHG_TYPE_SDP:
	case MT6370_CHG_TYPE_SDPNSTD:
		return CHARGE_SUPPLIER_BC12_SDP;
	case MT6370_CHG_TYPE_CDP:
		return CHARGE_SUPPLIER_BC12_CDP;
	case MT6370_CHG_TYPE_DCP:
	case MT6370_CHG_TYPE_SAMSUNG_CHARGER:
	case MT6370_CHG_TYPE_APPLE_0_5A_CHARGER:
	case MT6370_CHG_TYPE_APPLE_1_0A_CHARGER:
	case MT6370_CHG_TYPE_APPLE_2_1A_CHARGER:
	case MT6370_CHG_TYPE_APPLE_2_4A_CHARGER:
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		return CHARGE_SUPPLIER_NONE;
	}
}

/* Returns a mt6370 charger_type. */
static int mt6370_get_charger_type(int chgnum)
{
#ifdef CONFIG_CHARGER_MT6370
	int reg;

	if (rt946x_read8(chgnum, MT6370_REG_USBSTATUS1, &reg))
		return CHARGE_SUPPLIER_NONE;
	return mt6370_detect_apple_samsung_ta(chgnum, reg);
#else
	return CHARGE_SUPPLIER_NONE;
#endif
}

/*
 * The USB Type-C specification limits the maximum amount of current from BC 1.2
 * suppliers to 1.5A.  Technically, proprietary methods are not allowed, but we
 * will continue to allow those.
 */
static int mt6370_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case MT6370_CHG_TYPE_APPLE_0_5A_CHARGER:
		return 500;
	case MT6370_CHG_TYPE_APPLE_1_0A_CHARGER:
		return 1000;
	case MT6370_CHG_TYPE_APPLE_2_1A_CHARGER:
	case MT6370_CHG_TYPE_APPLE_2_4A_CHARGER:
	case MT6370_CHG_TYPE_DCP:
	case MT6370_CHG_TYPE_CDP:
	case MT6370_CHG_TYPE_SAMSUNG_CHARGER:
		return USB_CHARGER_MAX_CURR_MA;
	case MT6370_CHG_TYPE_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static int rt946x_get_bc12_device_type(int chgnum, int charger_type)
{
	int reg;

	if (rt946x_read8(chgnum, RT946X_REG_DPDM1, &reg))
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
}

static int rt946x_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		if (IS_ENABLED(CONFIG_CHARGE_RAMP_SW) ||
		    IS_ENABLED(CONFIG_CHARGE_RAMP_HW))
			/* A conservative value to prevent a bad charger. */
			return RT946X_AICR_TYP2MAX(USB_CHARGER_MAX_CURR_MA);
		__fallthrough;
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static void check_ac_state(void)
{
	static uint8_t ac;

	if (ac != extpower_is_present()) {
		ac = !ac;
		hook_notify(HOOK_AC_CHANGE);
	}
}
DECLARE_DEFERRED(check_ac_state);

void rt946x_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
	/*
	 * Generally, VBUS detection can be done immediately when the port
	 * plug/unplug happens. But if it's a PD plug(and will generate an
	 * interrupt), then it will take a few milliseconds to raise VBUS
	 * by PD negotiation.
	 */
	hook_call_deferred(&check_ac_state_data, 100 * MSEC);
}

int rt946x_toggle_bc12_detection(void)
{
	int rv;
	rv = rt946x_enable_bc12_detection(CHARGER_SOLO, 0);
	if (rv)
		return rv;
	/* mt6370 requires 40us delay to toggle RT946X_MASK_USBCHGEN */
	udelay(40);
	return rt946x_enable_bc12_detection(CHARGER_SOLO, 1);
}

static void check_pd_capable(void)
{
	const int port = TASK_ID_TO_USB_CHG_PORT(TASK_ID_USB_CHG);

	if (!pd_capable(port)) {
		enum tcpc_cc_voltage_status cc1, cc2;

		tcpm_get_cc(port, &cc1, &cc2);
		/* if CC is not changed. */
		if (cc_is_rp(cc1) || cc_is_rp(cc2))
			rt946x_toggle_bc12_detection();
	}
}
DECLARE_DEFERRED(check_pd_capable);

static void rt946x_usb_connect(void)
{
	const int port = TASK_ID_TO_USB_CHG_PORT(TASK_ID_USB_CHG);
	enum tcpc_cc_voltage_status cc1, cc2;

	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * Only detect BC1.2 device when USB-C device recognition is
	 * finished to prevent a potential race condition with USB enumeration.
	 * If CC exists RP, then it might be a BC12 or a PD capable device.
	 * Check this later to ensure it's not PD capable.
	 */
	if (cc_is_rp(cc1) || cc_is_rp(cc2))
		/* delay extra 50 ms to ensure SrcCap received */
		hook_call_deferred(&check_pd_capable_data,
				   PD_T_SINK_WAIT_CAP + 50 * MSEC);
	hook_call_deferred(&check_ac_state_data, 0);
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, rt946x_usb_connect, HOOK_PRIO_DEFAULT);

static void rt946x_pd_disconnect(void)
{
	/* Type-C disconnected, disable deferred check. */
	hook_call_deferred(&check_pd_capable_data, -1);
	hook_call_deferred(&check_ac_state_data, 0);
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, rt946x_pd_disconnect, HOOK_PRIO_DEFAULT);

int rt946x_get_adc(enum rt946x_adc_in_sel adc_sel, int *adc_val)
{
	int rv, i, adc_start, adc_result = 0;
	int adc_data_h, adc_data_l, aicr;
	const int max_wait_times = 6;

	if (in_interrupt_context()) {
		CPRINTS("Err: use ADC in IRQ");
		return EC_ERROR_INVAL;
	}
	mutex_lock(&adc_access_lock);
#ifdef CONFIG_CHARGER_MT6370
	mt6370_enable_hidden_mode(CHARGER_SOLO, 1);
#endif

	/* Select ADC to desired channel */
	rv = rt946x_update_bits(CHARGER_SOLO, RT946X_REG_CHGADC,
				RT946X_MASK_ADC_IN_SEL,
				adc_sel << RT946X_SHIFT_ADC_IN_SEL);
	if (rv)
		goto out;

	if (adc_sel == MT6370_ADC_IBUS) {
		rv = charger_get_input_current_limit(CHARGER_SOLO, &aicr);
		if (rv)
			goto out;
	}

	/* Start ADC conversation */
	rv = rt946x_set_bit(CHARGER_SOLO, RT946X_REG_CHGADC,
			    RT946X_MASK_ADC_START);
	if (rv)
		goto out;

	for (i = 0; i < max_wait_times; i++) {
		crec_msleep(35);
		rv = mt6370_pmu_reg_test_bit(CHARGER_SOLO, RT946X_REG_CHGADC,
					     RT946X_SHIFT_ADC_START,
					     &adc_start);
		if (!adc_start && rv == 0)
			break;
	}
	if (i == max_wait_times)
		CPRINTS("conversion fail sel=%d", adc_sel);

	/* Read ADC data */
	rv = rt946x_read8(CHARGER_SOLO, RT946X_REG_ADCDATAH, &adc_data_h);
	rv = rt946x_read8(CHARGER_SOLO, RT946X_REG_ADCDATAL, &adc_data_l);
	if (rv)
		goto out;

#if defined(CONFIG_CHARGER_RT9466) || defined(CONFIG_CHARGER_RT9467)
	if (adc_sel == RT946X_ADC_VBUS_DIV5)
		adc_result = ((adc_data_h << 8) | adc_data_l) * 25;
	else
		CPRINTS("unsupported channel %d", adc_sel);
	*adc_val = adc_result;
#elif defined(CONFIG_CHARGER_MT6370)
	/* Calculate ADC value */
	adc_result =
		(adc_data_h * 256 + adc_data_l) * mt6370_adc_unit[adc_sel] +
		mt6370_adc_offset[adc_sel];

	/* For TS_BAT/TS_BUS, the real unit is 0.25, here we use 25(unit) */
	if (adc_sel == MT6370_ADC_TS_BAT)
		adc_result /= 100;
#endif

out:
#ifdef CONFIG_CHARGER_MT6370
	if (adc_sel == MT6370_ADC_IBUS) {
		if (aicr < 400) /* 400mA */
			adc_result = adc_result * 67 / 100;
	}

	if (adc_sel != MT6370_ADC_TS_BAT && adc_sel != MT6370_ADC_TEMP_JC)
		*adc_val = adc_result / 1000;
	else
		*adc_val = adc_result;
	mt6370_enable_hidden_mode(CHARGER_SOLO, 0);
#endif
	mutex_unlock(&adc_access_lock);
	return rv;
}

static enum ec_error_list rt946x_get_vbus_voltage(int chgnum, int port,
						  int *voltage)
{
	int vbus_mv;
	int rv;

	rv = rt946x_get_adc(RT946X_ADC_VBUS_DIV5, &vbus_mv);
	*voltage = vbus_mv;

	return rv;
}

#ifdef CONFIG_CHARGER_MT6370
static int mt6370_toggle_cfo(void)
{
	int rv, data;

	rv = rt946x_read8(CHARGER_SOLO, MT6370_REG_FLEDEN, &data);
	if (rv)
		return rv;

	if (data & MT6370_STROBE_EN_MASK)
		return rv;

	/* read data */
	rv = rt946x_read8(CHARGER_SOLO, RT946X_REG_CHGCTRL2, &data);
	if (rv)
		return rv;

	/* cfo off */
	data &= ~RT946X_MASK_CFO_EN;
	rv = rt946x_write8(CHARGER_SOLO, RT946X_REG_CHGCTRL2, data);
	if (rv)
		return rv;

	/* cfo on */
	data |= RT946X_MASK_CFO_EN;
	return rt946x_write8(CHARGER_SOLO, RT946X_REG_CHGCTRL2, data);
}

static int mt6370_pmu_chg_mivr_irq_handler(int chgnum)
{
	int rv, ibus = 0, mivr_stat;

	rv = mt6370_pmu_reg_test_bit(chgnum, MT6370_REG_CHGSTAT1,
				     MT6370_SHIFT_MIVR_STAT, &mivr_stat);
	if (rv)
		return rv;

	if (!mivr_stat) {
		CPRINTS("no mivr stat");
		return rv;
	}

	rv = rt946x_get_adc(MT6370_ADC_IBUS, &ibus);
	if (rv)
		return rv;

	if (ibus < 100) /* 100mA */
		rv = mt6370_toggle_cfo();

	return rv;
}

static int mt6370_irq_handler(int chgnum)
{
	int data, mask, ret, reg_val;
	int stat_chg, valid_chg, stat_old, stat_new;

	ret = rt946x_write8(chgnum, MT6370_REG_IRQMASK, MT6370_IRQ_MASK_ALL);
	if (ret)
		return ret;

	ret = rt946x_read8(chgnum, MT6370_REG_IRQIND, &reg_val);
	if (ret)
		return ret;

	/* read stat before reading irq evt */
	ret = rt946x_read8(chgnum, MT6370_REG_CHGSTAT1, &stat_old);
	if (ret)
		return ret;

	/* workaround for irq, divided irq event into upper and lower */
	ret = rt946x_read8(chgnum, MT6370_REG_CHGIRQ1, &data);
	if (ret)
		return ret;

	/* read stat after reading irq evt */
	ret = rt946x_read8(chgnum, MT6370_REG_CHGSTAT1, &stat_new);
	if (ret)
		return ret;

	ret = rt946x_read8(chgnum, MT6370_REG_CHGMASK1, &mask);
	if (ret)
		return ret;

	ret = rt946x_write8(chgnum, MT6370_REG_IRQMASK, 0x00);
	if (ret)
		return ret;

	stat_chg = stat_old ^ stat_new;
	valid_chg = (stat_new & 0xF1) | (~stat_new & 0xF1);
	data |= (stat_chg & valid_chg);
	data &= ~mask;
	if (data)
		ret = mt6370_pmu_chg_mivr_irq_handler(chgnum);
	return ret;
}
#endif /* CONFIG_CHARGER_MT6370 */

static void rt946x_bc12_workaround(void)
{
	/*
	 * There is a parasitic capacitance on D+,
	 * which results in pulling D+ up too slow while detecting BC1.2.
	 * So we try to fix this in two steps:
	 * 1. Pull D+ up to a voltage under 0.6V
	 * 2. re-toggling and pull D+ up to 0.6V (again)
	 * and then detect the voltage of D-.
	 */
	rt946x_toggle_bc12_detection();
	crec_msleep(10);
	rt946x_toggle_bc12_detection();
}
DECLARE_DEFERRED(rt946x_bc12_workaround);

static void rt946x_usb_charger_task_init(const int unused_port)
{
	struct charge_port_info chg;
	int bc12_type = CHARGE_SUPPLIER_NONE;
	int chg_type;
	int reg = 0;
	int bc12_cnt = 0;
	const int max_bc12_cnt = 3;
	int voltage;

	chg.voltage = USB_CHARGER_VOLTAGE_MV;
	while (1) {
#ifdef CONFIG_CHARGER_MT6370
		mt6370_irq_handler(CHARGER_SOLO);
#endif /* CONFIG_CHARGER_MT6370 */

		rt946x_read8(CHARGER_SOLO, RT946X_REG_DPDMIRQ, &reg);

		/* VBUS attach event */
		if (reg & RT946X_MASK_DPDMIRQ_ATTACH) {
			charger_get_vbus_voltage(0, &voltage);
			CPRINTS("VBUS attached: %dmV", voltage);
			if (IS_ENABLED(CONFIG_CHARGER_MT6370)) {
				chg_type =
					mt6370_get_charger_type(CHARGER_SOLO);
				bc12_type =
					mt6370_get_bc12_device_type(chg_type);
				chg.current = mt6370_get_bc12_ilim(bc12_type);
			} else {
				bc12_type = rt946x_get_bc12_device_type(
					CHARGER_SOLO, chg_type);
				chg.current = rt946x_get_bc12_ilim(bc12_type);
			}
			CPRINTS("BC12 type %d", bc12_type);
			if (bc12_type == CHARGE_SUPPLIER_NONE)
				goto bc12_none;
			if (bc12_type == CHARGE_SUPPLIER_BC12_SDP &&
			    ++bc12_cnt < max_bc12_cnt) {
				/*
				 * defer the workaround and awaiting for
				 * waken up by the interrupt.
				 */
				hook_call_deferred(&rt946x_bc12_workaround_data,
						   5);
				goto wait_event;
			}

			charge_manager_update_charge(bc12_type, 0, &chg);
		bc12_none:
			rt946x_enable_bc12_detection(CHARGER_SOLO, 0);
		}

		/* VBUS detach event */
		if (reg & RT946X_MASK_DPDMIRQ_DETACH &&
		    bc12_type != CHARGE_SUPPLIER_NONE) {
			CPRINTS("VBUS detached");
			bc12_cnt = 0;
			charge_manager_update_charge(bc12_type, 0, NULL);
		}

	wait_event:
		task_wait_event(-1);
	}
}

static int rt946x_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP;
}

static int rt946x_ramp_max(int supplier, int sup_curr)
{
	return rt946x_get_bc12_ilim(supplier);
}
#endif /* HAS_TASK_USB_CHG */

/* Non-standard interface functions */

int rt946x_enable_charger_boost(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)(
		CHARGER_SOLO, RT946X_REG_CHGCTRL2, RT946X_MASK_CHG_EN);
}

/*
 * rt946x reports VBUS ready after VBUS is up for ~500ms.
 * Check if this works for the use case before calling this function.
 */
int rt946x_is_vbus_ready(void)
{
	int val = 0;

	return rt946x_read8(CHARGER_SOLO, RT946X_REG_CHGSTATC, &val) ?
		       0 :
		       !!(val & RT946X_MASK_PWR_RDY);
}

int rt946x_is_charge_done(void)
{
	int val = 0;

	if (rt946x_read8(CHARGER_SOLO, RT946X_REG_CHGSTAT, &val))
		return 0;

	val = (val & RT946X_MASK_CHG_STAT) >> RT946X_SHIFT_CHG_STAT;

	return val == RT946X_CHGSTAT_DONE;
}

int rt946x_cutoff_battery(void)
{
#ifdef CONFIG_CHARGER_MT6370
	/*
	 * We should lock ADC usage to prevent from using ADC while
	 * cut-off. Or this might cause the ADC power not turning off.
	 */

	int rv;

	mutex_lock(&adc_access_lock);
	rv = rt946x_write8(CHARGER_SOLO, MT6370_REG_RSTPASCODE1,
			   MT6370_MASK_RSTPASCODE1);
	if (rv)
		goto out;

	rv = rt946x_write8(CHARGER_SOLO, MT6370_REG_RSTPASCODE2,
			   MT6370_MASK_RSTPASCODE2);
	if (rv)
		goto out;

	/* reset all chg/fled/ldo/rgb/bl/db reg and logic */
	rv = rt946x_write8(CHARGER_SOLO, RT946X_REG_CORECTRL2, 0x7F);
	if (rv)
		goto out;

	/* disable chg auto sensing */
	mt6370_enable_hidden_mode(CHARGER_SOLO, 1);
	rv = rt946x_clr_bit(CHARGER_SOLO, MT6370_REG_CHGHIDDENCTRL15,
			    MT6370_MASK_ADC_TS_AUTO);
	mt6370_enable_hidden_mode(CHARGER_SOLO, 0);
	if (rv)
		goto out;
	crec_msleep(50);
	/* enter shipping mode */
	rv = rt946x_set_bit(CHARGER_SOLO, RT946X_REG_CHGCTRL2,
			    RT946X_MASK_SHIP_MODE);

out:
	mutex_unlock(&adc_access_lock);
	return rv;
#endif
	/* enter shipping mode */
	return rt946x_set_bit(CHARGER_SOLO, RT946X_REG_CHGCTRL2,
			      RT946X_MASK_SHIP_MODE);
}

int rt946x_enable_charge_termination(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)(
		CHARGER_SOLO, RT946X_REG_CHGCTRL2, RT946X_MASK_TE);
}

int rt946x_enable_charge_eoc(int en)
{
	return (en ? rt946x_set_bit : rt946x_clr_bit)(
		CHARGER_SOLO, RT946X_REG_CHGCTRL9, RT946X_MASK_EOC);
}

#ifdef CONFIG_CHARGER_MT6370
/* MT6370 LDO */

int mt6370_set_ldo_voltage(int mv)
{
	int rv;
	int vout_val;
	const int vout_mask = MT6370_MASK_LDOVOUT_EN | MT6370_MASK_LDOVOUT_VOUT;

	/* LDO output-off mode to floating. */
	rv = rt946x_update_bits(CHARGER_SOLO, MT6370_REG_LDOCFG,
				MT6370_MASK_LDOCFG_OMS, 0);
	if (rv)
		return rv;

	/* Disable LDO if voltage is zero. */
	if (mv == 0)
		return rt946x_clr_bit(CHARGER_SOLO, MT6370_REG_LDOVOUT,
				      MT6370_MASK_LDOVOUT_EN);

	vout_val = 1 << MT6370_SHIFT_LDOVOUT_EN;
	vout_val |= rt946x_closest_reg(MT6370_LDO_MIN, MT6370_LDO_MAX,
				       MT6370_LDO_STEP, mv);
	return rt946x_update_bits(CHARGER_SOLO, MT6370_REG_LDOVOUT, vout_mask,
				  vout_val);
}

/* MT6370 Display bias */
int mt6370_db_external_control(int en)
{
	return rt946x_update_bits(CHARGER_SOLO, MT6370_REG_DBCTRL1,
				  MT6370_MASK_DB_EXT_EN,
				  en << MT6370_SHIFT_DB_EXT_EN);
}

int mt6370_db_set_voltages(int vbst, int vpos, int vneg)
{
	int rv;

	/* set display bias VBST */
	rv = rt946x_update_bits(
		CHARGER_SOLO, MT6370_REG_DBVBST, MT6370_MASK_DB_VBST,
		rt946x_closest_reg(MT6370_DB_VBST_MIN, MT6370_DB_VBST_MAX,
				   MT6370_DB_VBST_STEP, vbst));

	/* set display bias VPOS */
	rv |= rt946x_update_bits(
		CHARGER_SOLO, MT6370_REG_DBVPOS, MT6370_MASK_DB_VPOS,
		rt946x_closest_reg(MT6370_DB_VPOS_MIN, MT6370_DB_VPOS_MAX,
				   MT6370_DB_VPOS_STEP, vpos));

	/* set display bias VNEG */
	rv |= rt946x_update_bits(
		CHARGER_SOLO, MT6370_REG_DBVNEG, MT6370_MASK_DB_VNEG,
		rt946x_closest_reg(MT6370_DB_VNEG_MIN, MT6370_DB_VNEG_MAX,
				   MT6370_DB_VNEG_STEP, vneg));

	/* Enable VNEG/VPOS discharge when VNEG/VPOS rails disabled. */
	rv |= rt946x_update_bits(
		CHARGER_SOLO, MT6370_REG_DBCTRL2,
		MT6370_MASK_DB_VNEG_DISC | MT6370_MASK_DB_VPOS_DISC,
		MT6370_MASK_DB_VNEG_DISC | MT6370_MASK_DB_VPOS_DISC);

	return rv;
}

/* MT6370 BACKLIGHT LED */

int mt6370_backlight_set_dim(uint16_t dim)
{
	int rv;

	/* datasheet suggests that update BLDIM2 first then BLDIM */
	rv = rt946x_write8(CHARGER_SOLO, MT6370_BACKLIGHT_BLDIM2,
			   dim & MT6370_MASK_BLDIM2);

	if (rv)
		return rv;

	rv = rt946x_write8(CHARGER_SOLO, MT6370_BACKLIGHT_BLDIM,
			   (dim >> MT6370_SHIFT_BLDIM_MSB) & MT6370_MASK_BLDIM);

	return rv;
}

/* MT6370 RGB LED */

int mt6370_led_set_dim_mode(enum mt6370_led_index index,
			    enum mt6370_led_dim_mode mode)
{
	if (index <= MT6370_LED_ID_OFF || index >= MT6370_LED_ID_COUNT)
		return EC_ERROR_INVAL;

	rt946x_update_bits(CHARGER_SOLO, MT6370_REG_RGBDIM_BASE + index,
			   MT6370_MASK_RGB_DIMMODE,
			   mode << MT6370_SHIFT_RGB_DIMMODE);
	return EC_SUCCESS;
}

int mt6370_led_set_color(uint8_t mask)
{
	return rt946x_update_bits(CHARGER_SOLO, MT6370_REG_RGBEN,
				  MT6370_MASK_RGB_ISNK_ALL_EN, mask);
}

int mt6370_led_set_brightness(enum mt6370_led_index index, uint8_t brightness)
{
	if (index >= MT6370_LED_ID_COUNT || index <= MT6370_LED_ID_OFF)
		return EC_ERROR_INVAL;

	rt946x_update_bits(CHARGER_SOLO, MT6370_REG_RGBISNK_BASE + index,
			   MT6370_MASK_RGBISNK_CURSEL,
			   brightness << MT6370_SHIFT_RGBISNK_CURSEL);
	return EC_SUCCESS;
}

int mt6370_led_set_pwm_dim_duty(enum mt6370_led_index index, uint8_t dim_duty)
{
	if (index >= MT6370_LED_ID_COUNT || index <= MT6370_LED_ID_OFF)
		return EC_ERROR_INVAL;

	rt946x_update_bits(CHARGER_SOLO, MT6370_REG_RGBDIM_BASE + index,
			   MT6370_MASK_RGB_DIMDUTY,
			   dim_duty << MT6370_SHIFT_RGB_DIMDUTY);
	return EC_SUCCESS;
}

int mt6370_led_set_pwm_frequency(enum mt6370_led_index index,
				 enum mt6370_led_pwm_freq freq)
{
	if (index >= MT6370_LED_ID_COUNT || index <= MT6370_LED_ID_OFF)
		return EC_ERROR_INVAL;

	rt946x_update_bits(CHARGER_SOLO, MT6370_REG_RGBISNK_BASE + index,
			   MT6370_MASK_RGBISNK_DIMFSEL,
			   freq << MT6370_SHIFT_RGBISNK_DIMFSEL);
	return EC_SUCCESS;
}

int mt6370_reduce_db_bl_driving(void)
{
	int rv;

	/* Enter test mode */
	rv = rt946x_block_write(CHARGER_SOLO, MT6370_REG_TM_PAS_CODE1,
				mt6370_val_en_test_mode,
				ARRAY_SIZE(mt6370_val_en_test_mode));
	if (rv)
		return rv;
	crec_msleep(1);
	rv = rt946x_write8(CHARGER_SOLO, MT6370_REG_BANK, MT6370_MASK_REG_TM);
	if (rv)
		return rv;
	crec_msleep(1);
	/* reduce bl driving */
	rv = rt946x_update_bits(CHARGER_SOLO, MT6370_TM_REG_BL3,
				MT6370_TM_MASK_BL3_SL, MT6370_TM_REDUCE_BL3_SL);
	if (rv)
		return rv;
	crec_msleep(1);
	/* reduce db driving */
	rv = rt946x_update_bits(CHARGER_SOLO, MT6370_TM_REG_DSV1,
				MT6370_TM_MASK_DSV1_SL,
				MT6370_TM_REDUCE_DSV1_SL);
	if (rv)
		return rv;
	crec_msleep(1);
	/* Leave test mode */
	return rt946x_write8(CHARGER_SOLO, MT6370_REG_TM_PAS_CODE1,
			     MT6370_LEAVE_TM);
}
#endif /* CONFIG_CHARGER_MT6370 */

const struct charger_drv rt946x_drv = {
	.init = &rt946x_init,
	.post_init = &rt946x_post_init,
	.get_info = &rt946x_get_info,
	.get_status = &rt946x_get_status,
	.set_mode = &rt946x_set_mode,
	.enable_otg_power = &rt946x_enable_otg_power,
	.is_sourcing_otg_power = &rt946x_is_sourcing_otg_power,
	.get_current = &rt946x_get_current,
	.set_current = &rt946x_set_current,
	.get_voltage = &rt946x_get_voltage,
	.set_voltage = &rt946x_set_voltage,
	.discharge_on_ac = &rt946x_discharge_on_ac,
	.get_vbus_voltage = &rt946x_get_vbus_voltage,
	.set_input_current_limit = &rt946x_set_input_current_limit,
	.get_input_current_limit = &rt946x_get_input_current_limit,
	.manufacturer_id = &rt946x_manufacturer_id,
	.device_id = &rt946x_device_id,
	.get_option = &rt946x_get_option,
	.set_option = &rt946x_set_option,
#ifdef CONFIG_CHARGE_RAMP_HW
	.set_hw_ramp = &rt946x_set_hw_ramp,
	.ramp_is_stable = &rt946x_ramp_is_stable,
	.ramp_is_detected = &rt946x_ramp_is_detected,
	.ramp_get_current_limit = &rt946x_ramp_get_current_limit,
#endif
};

#ifdef HAS_TASK_USB_CHG
const struct bc12_drv rt946x_bc12_drv = {
	.usb_charger_task_init = rt946x_usb_charger_task_init,
	/* events handled in init */
	.usb_charger_task_event = NULL,
	.ramp_allowed = rt946x_ramp_allowed,
	.ramp_max = rt946x_ramp_max,
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &rt946x_bc12_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
#endif
