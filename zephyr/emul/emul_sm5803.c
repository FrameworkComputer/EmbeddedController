/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "driver/charger/sm5803.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sm5803.h"
#include "emul/emul_stub_device.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT cros_sm5803_emul

LOG_MODULE_REGISTER(sm5803_emul, CONFIG_SM5803_EMUL_LOG_LEVEL);

#define VBUS_GPADC_LSB_MV 23.4
#define ADC_CURRENT_LSB_MA 7.32
#define ICL_LSB_MA 100
#define CHG_DET_THRESHOLD_MV 4000

struct sm5803_emul_data {
	struct i2c_common_emul_data i2c_main;
	struct i2c_common_emul_data i2c_chg;
	struct i2c_common_emul_data i2c_meas;
	struct i2c_common_emul_data i2c_test;

	/** Device ID register value */
	uint8_t device_id;
	/** PLATFORM register value */
	uint8_t pmode;
	/** Raw value of ISO_CL_REG1 */
	uint8_t input_current_limit;
	uint8_t gpadc_conf1, gpadc_conf2;
	/** Raw values of INT_EN{1..4} */
	uint8_t int_en[4];
	/** Raw values of INT_REQ_{1..4} */
	uint8_t irq1, irq2, irq3, irq4;
	uint16_t vbus;
	uint16_t ibus;
	uint16_t ibat_avg;
	bool clock_slowed;
	/** Measured VSYS voltage */
	uint16_t vsys;
	uint8_t cc_conf1;
	/** Raw values of FLOW_REG* */
	uint8_t flow1, flow2, flow3;
	/** Raw value of SWITCHER_CONF register */
	uint8_t switcher_conf;
	/** Bit 0 (PSYS_DAC_EN) of PSYS_REG1 */
	bool psys_dac_enabled;
	/** PHOT_REG1 raw value. */
	uint8_t phot1;
	/** PHOT_REG2 raw value. */
	uint8_t phot2;
	/** PHOT_REG3 raw value. */
	uint8_t phot3;
	/** PHOT_REG4 raw value. */
	uint8_t phot4;
	/** Raw values of DISCH_CONF_REG* */
	uint8_t disch_conf1, disch_conf2, disch_conf5, disch_conf6;
	/** Raw values of PRE_FAST_CONF_REG{1..6} */
	uint8_t pre_fast_conf[6];
	/** Raw value of GPIO0_CTRL register */
	uint8_t gpio_ctrl;
	/** Raw values of IR_COMP_REG1 and 2 */
	uint8_t ir_comp1, ir_comp2;
	/** Raw value of CHG_MON register */
	uint8_t chg_mon;
	/** Raw value of ANA_EN_REG1 register */
	uint8_t ana_en1;
	/** Raw value of STATUS_CHG register */
	uint8_t chg_status;
	/** Raw value of STATUS_DISCH register */
	uint8_t disch_status;
	/** Raw value of PORTS_CTRL register */
	uint8_t ports_ctrl;
	/** Raw value of REFERENCE1 register (REG_REFERENCE) */
	uint8_t reference1;
	/** Raw value of LOG_REG1 register (REG_LOG1) */
	uint8_t log1;
	/** Raw values of PREREG_CONF_REG{1,2} (REG_VSYS_PREREG_{M,L}LB) */
	uint8_t prereg_conf1, prereg_conf2;
	/** Voltage to report on the VBAT_SNSP pin, in mV */
	uint16_t vbat_sns_mv;
	/** Raw values of thermal threshold registers (TINT_{HIGH,LOW}_TH) */
	uint8_t tint_high_th, tint_low_th;
	/** Charger's reported internal temperature, in kelvin. */
	uint16_t internal_temp_kelvin;
};

struct sm5803_emul_cfg {
	const struct i2c_common_emul_cfg i2c_main;
	const struct i2c_common_emul_cfg i2c_chg;
	const struct i2c_common_emul_cfg i2c_meas;
	const struct i2c_common_emul_cfg i2c_test;
	const struct gpio_dt_spec *interrupt_gpio;
};

#define SIMPLE_GETTER(func_name, field_name)                         \
	uint8_t sm5803_emul_get_##func_name(const struct emul *emul) \
	{                                                            \
		const struct sm5803_emul_data *data = emul->data;    \
		return data->field_name;                             \
	}

const struct gpio_dt_spec *
sm5803_emul_get_interrupt_gpio(const struct emul *emul)
{
	const struct sm5803_emul_cfg *cfg = emul->cfg;

	return cfg->interrupt_gpio;
}

struct i2c_common_emul_data *sm5803_emul_get_i2c_main(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return &data->i2c_main;
}

struct i2c_common_emul_data *sm5803_emul_get_i2c_chg(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return &data->i2c_chg;
}

struct i2c_common_emul_data *sm5803_emul_get_i2c_meas(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return &data->i2c_meas;
}

struct i2c_common_emul_data *sm5803_emul_get_i2c_test(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return &data->i2c_test;
}

int sm5803_emul_read_chg_reg(const struct emul *emul, uint8_t reg)
{
	struct sm5803_emul_data *data = emul->data;

	switch (reg) {
	case SM5803_REG_CHG_ILIM:
		return data->input_current_limit;
	}
	return -ENOTSUP;
}

int sm5803_emul_get_fast_charge_current_limit(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return data->pre_fast_conf[3] & GENMASK(5, 0);
}

void sm5803_emul_set_vbus_voltage(const struct emul *emul, uint16_t mv)
{
	struct sm5803_emul_data *data = emul->data;
	uint16_t old = (float)data->vbus * VBUS_GPADC_LSB_MV;

	data->vbus = (uint16_t)((float)mv / VBUS_GPADC_LSB_MV);

	if (MIN(mv, old) <= CHG_DET_THRESHOLD_MV &&
	    MAX(mv, old) > CHG_DET_THRESHOLD_MV) {
		/* CHG_DET changes state; trigger an interrupt. */
		sm5803_emul_set_irqs(emul, SM5803_INT1_CHG, 0, 0, 0);
	}
}

void sm5803_emul_set_input_current(const struct emul *emul, uint16_t mv)
{
	struct sm5803_emul_data *data = emul->data;

	data->ibus = (uint16_t)((float)mv / ADC_CURRENT_LSB_MA);
}

void sm5803_emul_set_battery_current(const struct emul *emul, uint16_t ma)
{
	struct sm5803_emul_data *data = emul->data;

	data->ibat_avg = (uint16_t)((float)ma / ADC_CURRENT_LSB_MA);
}

static void update_interrupt_pin(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;
	const struct sm5803_emul_cfg *cfg = emul->cfg;

	bool pending = data->irq1 || data->irq2 || data->irq3 || data->irq4;

	/* Pin goes low if any IRQ is pending. */
	if (cfg->interrupt_gpio != NULL) {
		gpio_emul_input_set(cfg->interrupt_gpio->port,
				    cfg->interrupt_gpio->pin, !pending);
	}
}

void sm5803_emul_set_irqs(const struct emul *emul, uint8_t irq1, uint8_t irq2,
			  uint8_t irq3, uint8_t irq4)
{
	struct sm5803_emul_data *data = emul->data;

	data->irq1 |= irq1;
	data->irq2 |= irq2;
	data->irq3 |= irq3;
	data->irq4 |= irq4;
	update_interrupt_pin(emul);
}

static bool is_chg_det(struct sm5803_emul_data *data)
{
	/* Assume charger presence is cut off at 4V VBUS. */
	return data->vbus * VBUS_GPADC_LSB_MV > CHG_DET_THRESHOLD_MV;
}

void sm5803_emul_set_gpadc_conf(const struct emul *emul, uint8_t conf1,
				uint8_t conf2)
{
	struct sm5803_emul_data *data = emul->data;

	data->gpadc_conf1 = conf1;
	data->gpadc_conf2 = conf2;
}

void sm5803_emul_get_gpadc_conf(const struct emul *emul, uint8_t *conf1,
				uint8_t *conf2)
{
	struct sm5803_emul_data *data = emul->data;

	*conf1 = data->gpadc_conf1;
	*conf2 = data->gpadc_conf2;
}

bool sm5803_emul_is_clock_slowed(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return data->clock_slowed;
}

SIMPLE_GETTER(cc_config, cc_conf1)

void sm5803_emul_get_flow_regs(const struct emul *emul, uint8_t *flow1,
			       uint8_t *flow2, uint8_t *flow3)
{
	struct sm5803_emul_data *data = emul->data;

	if (flow1 != NULL) {
		*flow1 = data->flow1;
	}
	if (flow2 != NULL) {
		*flow2 = data->flow2;
	}
	if (flow3 != NULL) {
		*flow3 = data->flow3;
	}
}

void sm5803_emul_set_pmode(const struct emul *emul, uint8_t pmode)
{
	struct sm5803_emul_data *data = emul->data;

	data->pmode = pmode & GENMASK(4, 0);
}

void sm5803_emul_set_device_id(const struct emul *emul, uint8_t id)
{
	struct sm5803_emul_data *data = emul->data;

	data->device_id = id;
}

SIMPLE_GETTER(gpio_ctrl, gpio_ctrl)

uint16_t sm5803_emul_get_ir_comp(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return (data->ir_comp1 << 8) | data->ir_comp2;
}

SIMPLE_GETTER(chg_mon, chg_mon)
SIMPLE_GETTER(ana_en1, ana_en1)
SIMPLE_GETTER(disch_conf1, disch_conf1)
SIMPLE_GETTER(disch_conf2, disch_conf2)
SIMPLE_GETTER(disch_conf5, disch_conf5)
SIMPLE_GETTER(disch_conf6, disch_conf6)

SIMPLE_GETTER(chg_status, chg_status)

void sm5803_emul_set_chg_status(const struct emul *emul, uint8_t value)
{
	struct sm5803_emul_data *data = emul->data;

	data->chg_status = value;
}

SIMPLE_GETTER(disch_status, disch_status)

void sm5803_emul_set_disch_status(const struct emul *emul, uint8_t value)
{
	struct sm5803_emul_data *data = emul->data;

	data->disch_status = value;
}

SIMPLE_GETTER(ports_ctrl, ports_ctrl)
SIMPLE_GETTER(reference_reg, reference1)
SIMPLE_GETTER(log1, log1)

bool sm5803_emul_is_psys_dac_enabled(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;

	return data->psys_dac_enabled;
}

SIMPLE_GETTER(phot1, phot1)
SIMPLE_GETTER(phot2, phot2)
SIMPLE_GETTER(phot3, phot3)
SIMPLE_GETTER(phot4, phot4)

void sm5803_emul_set_vbat_sns_mv(const struct emul *emul, uint16_t mv)
{
	struct sm5803_emul_data *data = emul->data;

	data->vbat_sns_mv = mv;
}

uint16_t sm5803_emul_vbat_snsp_regval(const struct emul *emul, uint16_t mv)
{
	struct sm5803_emul_data *data = emul->data;
	double divisor;

	/* Volts per ADC LSb varies with number of battery cells. */
	if (data->pmode <= 5) { /* 1S */
		divisor = 5.13;
	} else if (data->pmode >= 6 && data->pmode <= 0xd) { /* 2S */
		divisor = 10.2;
	} else if (data->pmode >= 0xe && data->pmode <= 0x16) { /* 3S */
		divisor = 15.38;
	} else { /* 4S */
		divisor = 20.51;
	}

	return mv / divisor;
}

void sm5803_emul_set_internal_temperature(const struct emul *emul,
					  uint16_t kelvin)
{
	struct sm5803_emul_data *data = emul->data;

	data->internal_temp_kelvin = MIN(kelvin, 440);
}

SIMPLE_GETTER(tint_high_th, tint_high_th);
SIMPLE_GETTER(tint_low_th, tint_low_th);

void sm5803_emul_set_vsys_measured_mv(const struct emul *emul, uint16_t mv)
{
	struct sm5803_emul_data *data = emul->data;

	data->vsys = mv;
}

static void sm5803_emul_reset(const struct emul *emul)
{
	struct sm5803_emul_data *data = emul->data;
	const struct sm5803_emul_cfg *cfg = emul->cfg;

#define RESET_I2C(page)                                                  \
	do {                                                             \
		struct i2c_common_emul_data *common = &data->i2c_##page; \
                                                                         \
		i2c_common_emul_set_read_func(common, NULL, NULL);       \
		i2c_common_emul_set_write_func(common, NULL, NULL);      \
		i2c_common_emul_set_read_fail_reg(                       \
			common, I2C_COMMON_EMUL_NO_FAIL_REG);            \
		i2c_common_emul_set_write_fail_reg(                      \
			common, I2C_COMMON_EMUL_NO_FAIL_REG);            \
	} while (0)

	RESET_I2C(main);
	RESET_I2C(chg);
	RESET_I2C(meas);
	RESET_I2C(test);

	/* Registers set to chip reset values */
	data->device_id = 3;
	data->pmode = 0x0b;
	data->input_current_limit = 4;
	data->gpadc_conf1 = 0xf3;
	data->gpadc_conf2 = 0x01;
	memset(data->int_en, 0, sizeof(data->int_en));
	data->irq1 = data->irq2 = data->irq3 = data->irq4 = 0;
	data->vbus = 0;
	data->ibus = 0;
	data->ibat_avg = 0;
	data->clock_slowed = false;
	data->vsys = 0;
	data->cc_conf1 = 0x09;
	data->flow1 = 0x01;
	data->flow2 = 0;
	data->flow3 = 0;
	data->switcher_conf = 1;
	data->psys_dac_enabled = true;
	data->phot1 = 0x20;
	data->phot2 = 0x22;
	data->phot3 = 0xa;
	data->phot4 = 0x3f;
	data->disch_conf5 = 0;
	data->disch_conf6 = 0;
	memset(data->pre_fast_conf, 0, sizeof(data->pre_fast_conf));
	data->gpio_ctrl = 0x04;
	data->ir_comp1 = 1;
	data->ir_comp2 = 1;
	data->chg_mon = 0;
	data->ana_en1 = 0x99;
	data->disch_status = 0;
	data->reference1 = 0;
	data->log1 = 0;
	data->prereg_conf1 = 0;
	data->prereg_conf2 = 0;
	data->vbat_sns_mv = 0;
	data->tint_high_th = 0;
	data->tint_low_th = 0;
	data->internal_temp_kelvin = 296;

	/* Interrupt pin deasserted */
	if (cfg->interrupt_gpio != NULL) {
		gpio_emul_input_set(cfg->interrupt_gpio->port,
				    cfg->interrupt_gpio->pin, 1);
	}
}

static int sm5803_main_read_byte(const struct emul *target, int reg,
				 uint8_t *val, int bytes)
{
	struct sm5803_emul_data *data = target->data;

	switch (reg) {
	case SM5803_REG_CHIP_ID:
		*val = data->device_id;
		return 0;
	case SM5803_REG_STATUS1:
		*val = is_chg_det(data) ? SM5803_STATUS1_CHG_DET : 0;
		return 0;
	case SM5803_REG_INT1_REQ:
		*val = data->irq1;
		/* register clears on read */
		data->irq1 = 0;
		update_interrupt_pin(target);
		return 0;
	case SM5803_REG_INT2_REQ:
		*val = data->irq2;
		/* register clears on read */
		data->irq2 = 0;
		update_interrupt_pin(target);
		return 0;
	case SM5803_REG_INT3_REQ:
		*val = data->irq3;
		/* register clears on read */
		data->irq3 = 0;
		update_interrupt_pin(target);
		return 0;
	case SM5803_REG_INT4_REQ:
		*val = data->irq4;
		/* register clears on read */
		data->irq4 = 0;
		update_interrupt_pin(target);
		return 0;
	case SM5803_REG_INT1_EN:
	case SM5803_REG_INT2_EN:
	case SM5803_REG_INT3_EN:
	case SM5803_REG_INT4_EN:
		*val = data->int_en[reg - SM5803_REG_INT1_EN];
		return 0;
	case SM5803_REG_PLATFORM:
		*val = data->pmode;
		return 0;
	case SM5803_REG_REFERENCE:
		/* Driver never actually uses LDO PGOOD bits. */
		*val = 0;
		return 0;
	case SM5803_REG_CLOCK_SEL:
		*val = data->clock_slowed ? 1 : 0;
		return 0;
	case SM5803_REG_GPIO0_CTRL:
		*val = data->gpio_ctrl;
		return 0;
	case SM5803_REG_PORTS_CTRL:
		*val = data->ports_ctrl;
		return 0;
	}
	LOG_INF("SM5803 main page read of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_main_write_byte(const struct emul *target, int reg,
				  uint8_t val, int bytes)
{
	struct sm5803_emul_data *data = target->data;

	switch (reg) {
	case 0x1f: /* Mystery register used for linear charge enable. */
		return 0;
	case SM5803_REG_REFERENCE:
		data->reference1 = val & GENMASK(3, 0);
		return 0;
	case SM5803_REG_CLOCK_SEL:
		data->clock_slowed = (val & 1) == 1;
		return 0;
	case SM5803_REG_GPIO0_CTRL:
		data->gpio_ctrl = val & (GENMASK(7, 6) | GENMASK(2, 0));
		return 0;
	case SM5803_REG_PORTS_CTRL:
		/* Bits 4-7 always read 0 */
		data->ports_ctrl = val & GENMASK(3, 0);
		return 0;
	}
	LOG_INF("SM5803 main page write of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_chg_read_byte(const struct emul *target, int reg,
				uint8_t *val, int bytes)
{
	struct sm5803_emul_data *data = target->data;

	switch (reg) {
	case SM5803_REG_CC_CONFIG1:
		*val = data->cc_conf1;
		return 0;
	case SM5803_REG_INT1_EN:
	case SM5803_REG_INT2_EN:
	case SM5803_REG_INT3_EN:
	case SM5803_REG_INT4_EN:
		*val = data->int_en[reg - SM5803_REG_INT1_EN];
		return 0;
	case SM5803_REG_FLOW1:
		*val = data->flow1;
		return 0;
	case SM5803_REG_FLOW2:
		*val = data->flow2;
		return 0;
	case SM5803_REG_FLOW3:
		*val = data->flow3;
		return 0;
	case SM5803_REG_SWITCHER_CONF:
		*val = data->switcher_conf;
		return 0;
	case SM5803_REG_ANA_EN1:
		*val = data->ana_en1;
		return 0;
	case SM5803_REG_CHG_ILIM:
		*val = data->input_current_limit;
		return 0;
	case SM5803_REG_VPWR_MSB:
		*val = data->disch_conf1;
		return 0;
	case SM5803_REG_DISCH_CONF2:
		*val = data->disch_conf2;
		return 0;
	case SM5803_REG_DISCH_CONF5:
		*val = data->disch_conf5;
		return 0;
	case SM5803_REG_DISCH_CONF6:
		*val = data->disch_conf6;
		return 0;
	case SM5803_REG_PRE_FAST_CONF_REG1:
	case SM5803_REG_PRE_FAST_CONF_REG1 + 1: /* VBAT_FAST_MSB */
	case SM5803_REG_PRE_FAST_CONF_REG1 + 2: /* VBAT_FAST_LSB */
	case SM5803_REG_PRE_FAST_CONF_REG1 + 3: /* FAST_CONF4 */
	case SM5803_REG_PRE_FAST_CONF_REG1 + 4: /* FAST_CONF5 */
	case SM5803_REG_PRE_FAST_CONF_REG1 + 5:
		*val = data->pre_fast_conf[reg - SM5803_REG_PRE_FAST_CONF_REG1];
		return 0;
	case SM5803_REG_IR_COMP1:
		*val = data->ir_comp1;
		return 0;
	case SM5803_REG_IR_COMP2:
		*val = data->ir_comp2;
		return 0;
	case SM5803_REG_LOG1:
		*val = data->log1;
		return 0;
	case SM5803_REG_LOG2:
		*val = ((data->ibus * ADC_CURRENT_LSB_MA) >
			(data->input_current_limit * ICL_LSB_MA))
		       << 1;
		return 0;
	case SM5803_REG_STATUS_CHG_REG:
		*val = data->chg_status;
		return 0;
	case SM5803_REG_STATUS_DISCHG:
		*val = data->disch_status;
		return 0;
	case SM5803_REG_CHG_MON_REG:
		*val = data->chg_mon;
		return 0;
	case SM5803_REG_PHOT1:
		*val = data->phot1;
		return 0;
	case SM5803_REG_PHOT2:
		*val = data->phot2;
		return 0;
	case SM5803_REG_PHOT3:
		*val = data->phot3;
		return 0;
	case SM5803_REG_PHOT4:
		*val = data->phot4;
		return 0;
	}
	LOG_INF("SM5803 charger page read of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_chg_write_byte(const struct emul *target, int reg,
				 uint8_t val, int bytes)
{
	struct sm5803_emul_data *data = target->data;

	switch (reg) {
	case SM5803_REG_CC_CONFIG1:
		data->cc_conf1 = val;
		return 0;
	case SM5803_REG_FLOW1:
		data->flow1 = val & 0x8f;
		/* Enabling linear charge turns on the BATFET. */
		if (val & SM5803_FLOW1_LINEAR_CHARGE_EN) {
			data->log1 |= SM5803_BATFET_ON;
		}
		return 0;
	case SM5803_REG_FLOW2:
		data->flow2 = val;
		return 0;
	case SM5803_REG_FLOW3:
		data->flow3 = val & GENMASK(6, 0);
		return 0;
	case SM5803_REG_SWITCHER_CONF:
		data->switcher_conf = val & 0xc1;
		return 0;
	case SM5803_REG_ANA_EN1:
		data->ana_en1 = val;
		return 0;
	case SM5803_REG_CHG_ILIM:
		data->input_current_limit = val & GENMASK(4, 0);
		return 0;
	case SM5803_REG_VPWR_MSB:
		data->disch_conf1 = val;
		return 0;
	case SM5803_REG_DISCH_CONF2:
		data->disch_conf2 = val;
		return 0;
	case SM5803_REG_DISCH_CONF5:
		data->disch_conf5 = val;
		return 0;
	case SM5803_REG_DISCH_CONF6:
		data->disch_conf6 = val;
		return 0;
	case SM5803_REG_VSYS_PREREG_MSB:
		data->prereg_conf1 = val;
		return 0;
	case SM5803_REG_VSYS_PREREG_LSB:
		data->prereg_conf2 = val;
		return 0;
	case SM5803_REG_PRE_FAST_CONF_REG1:
	case SM5803_REG_PRE_FAST_CONF_REG1 + 1:
	case SM5803_REG_PRE_FAST_CONF_REG1 + 2:
	case SM5803_REG_PRE_FAST_CONF_REG1 + 3:
	case SM5803_REG_PRE_FAST_CONF_REG1 + 4:
	case SM5803_REG_PRE_FAST_CONF_REG1 + 5:
		data->pre_fast_conf[reg - SM5803_REG_PRE_FAST_CONF_REG1] = val;
		return 0;
	case SM5803_REG_IR_COMP1:
		data->ir_comp1 = val;
		return 0;
	case SM5803_REG_IR_COMP2:
		data->ir_comp2 = val;
		return 0;
	case SM5803_REG_STATUS_CHG_REG:
		/* Bits are cleared when written set */
		data->chg_status &= ~val;
		return 0;
	case SM5803_REG_STATUS_DISCHG:
		/* Bits are cleared when written set */
		data->disch_status &= ~val;
		return 0;
	case SM5803_REG_PHOT1:
		data->phot1 = val;
		return 0;
	case SM5803_REG_PHOT2:
		data->phot2 = val;
		return 0;
	case SM5803_REG_PHOT3:
		data->phot3 = val;
		return 0;
	case SM5803_REG_PHOT4:
		data->phot4 = val;
		return 0;
	case SM5803_REG_CHG_MON_REG:
		data->chg_mon = val;
		return 0;
	}
	LOG_INF("SM5803 charger page write of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_meas_read_byte(const struct emul *target, int reg,
				 uint8_t *val, int bytes)
{
	struct sm5803_emul_data *data = target->data;

	switch (reg) {
	case SM5803_REG_GPADC_CONFIG1:
		*val = data->gpadc_conf1;
		return 0;
	case SM5803_REG_GPADC_CONFIG2:
		*val = data->gpadc_conf2;
		return 0;
	case SM5803_REG_PSYS1:
		*val = 0x04 | data->psys_dac_enabled;
		return 0;
	case SM5803_REG_VBATSNSP_MAX_TH:
		/* Unimportant; tests don't care what this register reads. */
		*val = 0;
		return 0;
	case SM5803_REG_TINT_HIGH_TH:
		*val = data->tint_high_th;
		return 0;
	case SM5803_REG_TINT_LOW_TH:
		*val = data->tint_low_th;
		return 0;
	case SM5803_REG_VBATSNSP_MEAS_MSB:
		*val = (sm5803_emul_vbat_snsp_regval(target,
						     data->vbat_sns_mv) &
			GENMASK(9, 2)) >>
		       2;
		return 0;
	case SM5803_REG_VBATSNSP_MEAS_LSB:
		*val = sm5803_emul_vbat_snsp_regval(target, data->vbat_sns_mv) &
		       GENMASK(1, 0);
		return 0;
	case SM5803_REG_IBUS_CHG_MEAS_MSB:
		*val = (data->ibus & GENMASK(9, 2)) >> 2;
		return 0;
	case SM5803_REG_IBUS_CHG_MEAS_LSB:
		*val = data->ibus & GENMASK(1, 0);
		return 0;
	case SM5803_REG_VBUS_MEAS_MSB:
		*val = (data->vbus & GENMASK(9, 2)) >> 2;
		return 0;
	case SM5803_REG_VBUS_MEAS_LSB:
		*val = (is_chg_det(data) ? SM5803_VBUS_MEAS_CHG_DET : 0) |
		       (data->vbus & GENMASK(1, 0));
		return 0;
	case SM5803_REG_TINT_MEAS_MSB:
		*val = (uint16_t)(data->internal_temp_kelvin / 0.43) >> 2;
		return 0;
		/* Driver doesn't use TINT_MEAS_LSB, so unimplemented. */
	case SM5803_REG_IBAT_CHG_AVG_MEAS_MSB:
		*val = (data->ibat_avg & GENMASK(9, 2)) >> 2;
		return 0;
	case SM5803_REG_IBAT_CHG_AVG_MEAS_LSB:
		*val = data->ibat_avg & GENMASK(1, 0);
		return 0;
	case SM5803_REG_VSYS_AVG_MEAS_MSB:
		/* 23.4 mV per LSb */
		*val = (uint16_t)(data->vsys / 23.4) >> 2;
		return 0;
	case SM5803_REG_VSYS_AVG_MEAS_LSB:
		*val = (uint16_t)(data->vsys / 23.4) & GENMASK(1, 0);
		return 0;
	}
	LOG_INF("SM5803 meas page read of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_meas_write_byte(const struct emul *target, int reg,
				  uint8_t val, int bytes)
{
	struct sm5803_emul_data *data = target->data;

	switch (reg) {
	case SM5803_REG_PSYS1:
		data->psys_dac_enabled = (val & 1) != 0;
		return 0;
	case SM5803_REG_GPADC_CONFIG1:
		data->gpadc_conf1 = val;
		return 0;
	case SM5803_REG_GPADC_CONFIG2:
		data->gpadc_conf2 = val;
		return 0;
	case SM5803_REG_TINT_HIGH_TH:
		data->tint_high_th = val;
		return 0;
	case SM5803_REG_TINT_LOW_TH:
		data->tint_low_th = val;
		return 0;
	}

	LOG_INF("SM5803 meas page write of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_test_read_byte(const struct emul *target, int reg,
				 uint8_t *val, int bytes)
{
	switch (reg) {
	case 0x8e: /* Mystery register used for init on chip ID 2 */
		*val = 0;
		return 0;
	}
	LOG_INF("SM5803 test page read of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_test_write_byte(const struct emul *target, int reg,
				  uint8_t val, int bytes)
{
	switch (reg) {
	case 0x44: /* Mystery register used for linear charge enable. */
		return 0;
	case 0x8e: /* Mystery register used for init on chip ID 2 */
		return 0;
	}
	LOG_INF("SM5803 test page write of register %#x unhandled", reg);
	return -ENOTSUP;
}

static int sm5803_emul_i2c_transfer(const struct emul *target,
				    struct i2c_msg *msgs, int num_msgs,
				    int addr)
{
	struct sm5803_emul_data *data = target->data;
	const struct sm5803_emul_cfg *cfg = target->cfg;

	if (addr == cfg->i2c_main.addr) {
		return i2c_common_emul_transfer_workhorse(target,
							  &data->i2c_main,
							  &cfg->i2c_main, msgs,
							  num_msgs, addr);
	} else if (addr == cfg->i2c_chg.addr) {
		return i2c_common_emul_transfer_workhorse(target,
							  &data->i2c_chg,
							  &cfg->i2c_chg, msgs,
							  num_msgs, addr);
	} else if (addr == cfg->i2c_meas.addr) {
		return i2c_common_emul_transfer_workhorse(target,
							  &data->i2c_meas,
							  &cfg->i2c_meas, msgs,
							  num_msgs, addr);
	} else if (addr == cfg->i2c_test.addr) {
		return i2c_common_emul_transfer_workhorse(target,
							  &data->i2c_test,
							  &cfg->i2c_test, msgs,
							  num_msgs, addr);
	}
	LOG_ERR("I2C transaction for address %#x not supported by SM5803",
		addr);
	return -ENOTSUP;
}

const static struct i2c_emul_api sm5803_emul_api = {
	.transfer = sm5803_emul_i2c_transfer,
};

static int sm5803_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct sm5803_emul_data *data = emul->data;
	struct i2c_common_emul_data *const i2c_pages[] = {
		&data->i2c_chg,
		&data->i2c_main,
		&data->i2c_meas,
		&data->i2c_test,
	};

	for (int i = 0; i < ARRAY_SIZE(i2c_pages); i++) {
		int rv = i2c_emul_register(parent, &i2c_pages[i]->emul);

		if (rv != 0) {
			k_oops();
		}
		i2c_common_emul_init(i2c_pages[i]);
	}

	sm5803_emul_reset(emul);

	return 0;
}

#define INST_HAS_IRQ(n) DT_INST_NODE_HAS_PROP(n, interrupt_gpios)

#define INIT_SM5803(n)                                                           \
	const static struct sm5803_emul_cfg sm5803_emul_cfg_##n;                 \
	static struct sm5803_emul_data sm5803_emul_data_##n = {                \
		.i2c_main = {						       \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(n)),               \
			.emul =                                                \
				(struct i2c_emul){                             \
					.target = EMUL_DT_GET(                 \
						DT_DRV_INST(n)),               \
					.api = &sm5803_emul_api,               \
					.addr = DT_INST_PROP(                  \
						n, main_addr),                 \
				},                                             \
			.cfg = &sm5803_emul_cfg_##n.i2c_main,                  \
			.read_byte = &sm5803_main_read_byte,                   \
			.write_byte = &sm5803_main_write_byte,                 \
		},                                                             \
		.i2c_chg = {						       \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(n)),               \
			.emul =                                                \
				(struct i2c_emul){                             \
					.target = EMUL_DT_GET(                 \
						DT_DRV_INST(n)),               \
					.api = &sm5803_emul_api,               \
					.addr = DT_INST_REG_ADDR(n),           \
				},                                             \
			.cfg = &sm5803_emul_cfg_##n.i2c_chg,                   \
			.read_byte = &sm5803_chg_read_byte,                    \
			.write_byte = &sm5803_chg_write_byte,                  \
		},                                                             \
		.i2c_meas = {						       \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(n)),               \
			.emul =                                                \
				(struct i2c_emul){                             \
					.target = EMUL_DT_GET(                 \
						DT_DRV_INST(n)),               \
					.api = &sm5803_emul_api,               \
					.addr = DT_INST_PROP(                  \
						n, meas_addr),                 \
				},                                             \
			.cfg = &sm5803_emul_cfg_##n.i2c_meas,                  \
			.read_byte = &sm5803_meas_read_byte,                   \
			.write_byte = &sm5803_meas_write_byte,                 \
		},                                                             \
		.i2c_test = {						       \
			.i2c = DEVICE_DT_GET(DT_INST_PARENT(n)),               \
			.emul =                                                \
				(struct i2c_emul){                             \
					.target = EMUL_DT_GET(                 \
						DT_DRV_INST(n)),               \
					.api = &sm5803_emul_api,               \
					.addr = DT_INST_PROP(                  \
						n, test_addr),                 \
				},                                             \
			.cfg = &sm5803_emul_cfg_##n.i2c_test,                  \
			.read_byte = &sm5803_test_read_byte,                   \
			.write_byte = &sm5803_test_write_byte,                 \
		},                                                             \
	}; \
	IF_ENABLED(INST_HAS_IRQ(n),                                              \
		   (const static struct gpio_dt_spec sm5803_emul_irq_##n = {     \
			    .port = DEVICE_DT_GET(DT_GPIO_CTLR(                  \
				    DT_DRV_INST(n), interrupt_gpios)),           \
			    .pin = DT_INST_GPIO_PIN(n, interrupt_gpios),         \
			    .dt_flags = (gpio_dt_flags_t)DT_INST_GPIO_FLAGS(     \
				    n, interrupt_gpios),                         \
		    };))                                                         \
	const static struct sm5803_emul_cfg sm5803_emul_cfg_##n = {              \
		.i2c_main =                                                      \
			(struct i2c_common_emul_cfg){                            \
				.dev_label =                                     \
					DT_NODE_FULL_NAME(DT_DRV_INST(n)),       \
				.addr = DT_INST_PROP(n, main_addr),              \
				.data = &sm5803_emul_data_##n.i2c_main,          \
			},                                                       \
		.i2c_chg =                                                       \
			(struct i2c_common_emul_cfg){                            \
				.dev_label =                                     \
					DT_NODE_FULL_NAME(DT_DRV_INST(n)),       \
				.addr = DT_INST_REG_ADDR(n),                     \
				.data = &sm5803_emul_data_##n.i2c_chg,           \
			},                                                       \
		.i2c_meas =                                                      \
			(struct i2c_common_emul_cfg){                            \
				.dev_label =                                     \
					DT_NODE_FULL_NAME(DT_DRV_INST(n)),       \
				.addr = DT_INST_PROP(n, meas_addr),              \
				.data = &sm5803_emul_data_##n.i2c_meas,          \
			},                                                       \
		.i2c_test =                                                      \
			(struct i2c_common_emul_cfg){                            \
				.dev_label =                                     \
					DT_NODE_FULL_NAME(DT_DRV_INST(n)),       \
				.addr = DT_INST_PROP(n, test_addr),              \
				.data = &sm5803_emul_data_##n.i2c_test,          \
			},                                                       \
		.interrupt_gpio = COND_CODE_1(INST_HAS_IRQ(n),                   \
					      (&sm5803_emul_irq_##n), (NULL)),   \
	};                                                                       \
	EMUL_DT_INST_DEFINE(n, sm5803_emul_init, &sm5803_emul_data_##n,          \
			    &sm5803_emul_cfg_##n, &sm5803_emul_api, NULL);

DT_INST_FOREACH_STATUS_OKAY(INIT_SM5803)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE)

static void sm5803_emul_reset_before(const struct ztest_unit_test *test,
				     void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

#define SM5803_EMUL_RESET_RULE_BEFORE(n) \
	sm5803_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)));

	DT_INST_FOREACH_STATUS_OKAY(SM5803_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(sm5803_emul_reset, sm5803_emul_reset_before, NULL);
