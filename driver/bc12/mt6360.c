/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "charge_manager.h"
#include "console.h"
#include "crc8.h"
#include "driver/bc12/mt6360.h"
#include "ec_commands.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) \
	cprints(CC_USBCHARGE, "%s " format, "MT6360", ## args)

static enum ec_error_list mt6360_read8(int reg, int *val)
{
	return i2c_read8(mt6360_config.i2c_port, mt6360_config.i2c_addr_flags,
			reg, val);
}

static enum ec_error_list mt6360_write8(int reg, int val)
{
	return i2c_write8(mt6360_config.i2c_port, mt6360_config.i2c_addr_flags,
			reg, val);
}

static int mt6360_update_bits(int reg, int mask, int val)
{
	int rv;
	int reg_val;

	rv = mt6360_read8(reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = mt6360_write8(reg, reg_val);
	return rv;
}

static inline int mt6360_set_bit(int reg, int mask)
{
	return mt6360_update_bits(reg, mask, mask);
}

static inline int mt6360_clr_bit(int reg, int mask)
{
	return mt6360_update_bits(reg, mask, 0x00);
}

static int mt6360_get_bc12_device_type(void)
{
	int reg;

	if (mt6360_read8(MT6360_REG_USB_STATUS_1, &reg))
		return CHARGE_SUPPLIER_NONE;

	switch (reg & MT6360_MASK_USB_STATUS) {
	case MT6360_MASK_SDP:
		CPRINTS("BC12 SDP");
		return CHARGE_SUPPLIER_BC12_SDP;
	case MT6360_MASK_CDP:
		CPRINTS("BC12 CDP");
		return CHARGE_SUPPLIER_BC12_CDP;
	case MT6360_MASK_DCP:
		CPRINTS("BC12 DCP");
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		CPRINTS("BC12 NONE");
		return CHARGE_SUPPLIER_NONE;
	}
}

static int mt6360_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static int mt6360_enable_bc12_detection(int en)
{
	int rv;

	if (en) {
#ifdef CONFIG_MT6360_BC12_GPIO
		gpio_set_level(GPIO_BC12_DET_EN, 1);
#endif
		return mt6360_set_bit(MT6360_REG_DEVICE_TYPE,
				      MT6360_MASK_USBCHGEN);
	}

	rv = mt6360_clr_bit(MT6360_REG_DEVICE_TYPE, MT6360_MASK_USBCHGEN);
#ifdef CONFIG_MT6360_BC12_GPIO
	gpio_set_level(GPIO_BC12_DET_EN, 0);
#endif
	return rv;
}

static void mt6360_update_charge_manager(int port,
					 enum charge_supplier new_bc12_type)
{
	static enum charge_supplier current_bc12_type = CHARGE_SUPPLIER_NONE;

	if (new_bc12_type != current_bc12_type) {
		charge_manager_update_charge(current_bc12_type, port, NULL);

		if (new_bc12_type != CHARGE_SUPPLIER_NONE) {
			struct charge_port_info chg = {
				.current = mt6360_get_bc12_ilim(new_bc12_type),
				.voltage = USB_CHARGER_VOLTAGE_MV,
			};

			charge_manager_update_charge(new_bc12_type, port, &chg);
		}

		current_bc12_type = new_bc12_type;
	}
}

static void mt6360_handle_bc12_irq(int port)
{
	int reg;

	mt6360_read8(MT6360_REG_DPDMIRQ, &reg);

	if (reg & MT6360_MASK_DPDMIRQ_ATTACH) {
		/* Check vbus again to avoid timing issue */
		if (pd_snk_is_vbus_provided(port))
			mt6360_update_charge_manager(
					port, mt6360_get_bc12_device_type());
		else
			mt6360_update_charge_manager(
					0, CHARGE_SUPPLIER_NONE);
	}

	/* write clear */
	mt6360_write8(MT6360_REG_DPDMIRQ, reg);
}

static void mt6360_usb_charger_task(const int port)
{
	mt6360_clr_bit(MT6360_REG_DPDM_MASK1,
		       MT6360_REG_DPDM_MASK1_CHGDET_DONEI_M);
	mt6360_enable_bc12_detection(0);

	while (1) {
		uint32_t evt = task_wait_event(-1);

		/* vbus change, start bc12 detection */
		if (evt & USB_CHG_EVENT_VBUS) {
			if (pd_snk_is_vbus_provided(port))
				mt6360_enable_bc12_detection(1);
			else
				mt6360_update_charge_manager(
						0, CHARGE_SUPPLIER_NONE);
		}

		/* detection done, update charge_manager and stop detection */
		if (evt & USB_CHG_EVENT_BC12) {
			mt6360_handle_bc12_irq(port);
			mt6360_enable_bc12_detection(0);
		}
	}
}

/* Regulator: LDO & BUCK */
static int mt6360_regulator_write8(uint8_t addr, int reg, int val)
{
	/*
	 * Note: The checksum from I2C_FLAG_PEC happens to be correct because
	 * length == 1 -> the high 3 bits of the offset byte is 0.
	 */
	return i2c_write8(mt6360_config.i2c_port,
			  addr | I2C_FLAG_PEC, reg, val);
}

static int mt6360_regulator_read8(int addr, int reg, int *val)
{
	int rv;
	uint8_t crc = 0, real_crc;
	uint8_t out[3] = {(addr << 1) | 1, reg};

	rv = i2c_read16(mt6360_config.i2c_port, addr, reg, val);
	if (rv)
		return rv;

	real_crc = (*val >> 8) & 0xFF;
	*val &= 0xFF;
	out[2] = *val;
	crc = crc8(out, ARRAY_SIZE(out));

	if (crc != real_crc)
		return EC_ERROR_CRC;

	return EC_SUCCESS;
}

static int mt6360_regulator_update_bits(int addr, int reg, int mask, int val)
{
	int rv;
	int reg_val = 0;

	rv = mt6360_regulator_read8(addr, reg, &reg_val);
	if (rv)
		return rv;
	reg_val &= ~mask;
	reg_val |= (mask & val);
	rv = mt6360_regulator_write8(addr, reg, reg_val);
	return rv;
}

struct mt6360_regulator_data {
	const char *name;
	const uint16_t *ldo_vosel_table;
	uint16_t ldo_vosel_table_len;
	uint8_t addr;
	uint8_t reg_en_ctrl2;
	uint8_t reg_ctrl3;
	uint8_t mask_vosel;
	uint8_t shift_vosel;
	uint8_t mask_vocal;
};

static const uint16_t MT6360_LDO3_VOSEL_TABLE[16] = {
	[0x4] = 1800,
	[0xA] = 2900,
	[0xB] = 3000,
	[0xD] = 3300,
};

static const uint16_t MT6360_LDO5_VOSEL_TABLE[8] = {
	[0x2] = 2900,
	[0x3] = 3000,
	[0x5] = 3300,
};

static const uint16_t MT6360_LDO6_VOSEL_TABLE[16] = {
	[0x0] = 500,
	[0x1] = 600,
	[0x2] = 700,
	[0x3] = 800,
	[0x4] = 900,
	[0x5] = 1000,
	[0x6] = 1100,
	[0x7] = 1200,
	[0x8] = 1300,
	[0x9] = 1400,
	[0xA] = 1500,
	[0xB] = 1600,
	[0xC] = 1700,
	[0xD] = 1800,
	[0xE] = 1900,
	[0xF] = 2000,
};

/* LDO7 VOSEL table is the same as LDO6's. */
static const uint16_t *const MT6360_LDO7_VOSEL_TABLE = MT6360_LDO6_VOSEL_TABLE;
static const uint16_t MT6360_LDO7_VOSEL_TABLE_SIZE =
	ARRAY_SIZE(MT6360_LDO6_VOSEL_TABLE);

static const
struct mt6360_regulator_data regulator_data[MT6360_REGULATOR_COUNT] = {
	[MT6360_LDO3] = {
		.name = "mt6360_ldo3",
		.ldo_vosel_table = MT6360_LDO3_VOSEL_TABLE,
		.ldo_vosel_table_len = ARRAY_SIZE(MT6360_LDO3_VOSEL_TABLE),
		.addr = MT6360_LDO_SLAVE_ADDR_FLAGS,
		.reg_en_ctrl2 = MT6360_REG_LDO3_EN_CTRL2,
		.reg_ctrl3 = MT6360_REG_LDO3_CTRL3,
		.mask_vosel = MT6360_MASK_LDO3_VOSEL,
		.shift_vosel = MT6360_MASK_LDO3_VOSEL_SHIFT,
		.mask_vocal = MT6360_MASK_LDO3_VOCAL,
	},
	[MT6360_LDO5] = {
		.name = "mt6360_ldo5",
		.ldo_vosel_table = MT6360_LDO5_VOSEL_TABLE,
		.ldo_vosel_table_len = ARRAY_SIZE(MT6360_LDO5_VOSEL_TABLE),
		.addr = MT6360_LDO_SLAVE_ADDR_FLAGS,
		.reg_en_ctrl2 = MT6360_REG_LDO5_EN_CTRL2,
		.reg_ctrl3 = MT6360_REG_LDO5_CTRL3,
		.mask_vosel = MT6360_MASK_LDO5_VOSEL,
		.shift_vosel = MT6360_MASK_LDO5_VOSEL_SHIFT,
		.mask_vocal = MT6360_MASK_LDO5_VOCAL,
	},
	[MT6360_LDO6] = {
		.name = "mt6360_ldo6",
		.ldo_vosel_table = MT6360_LDO6_VOSEL_TABLE,
		.ldo_vosel_table_len = ARRAY_SIZE(MT6360_LDO6_VOSEL_TABLE),
		.addr = MT6360_PMIC_SLAVE_ADDR_FLAGS,
		.reg_en_ctrl2 = MT6360_REG_LDO6_EN_CTRL2,
		.reg_ctrl3 = MT6360_REG_LDO6_CTRL3,
		.mask_vosel = MT6360_MASK_LDO6_VOSEL,
		.shift_vosel = MT6360_MASK_LDO6_VOSEL_SHIFT,
		.mask_vocal = MT6360_MASK_LDO6_VOCAL,
	},
	[MT6360_LDO7] = {
		.name = "mt6360_ldo7",
		.ldo_vosel_table = MT6360_LDO7_VOSEL_TABLE,
		.ldo_vosel_table_len = MT6360_LDO7_VOSEL_TABLE_SIZE,
		.addr = MT6360_PMIC_SLAVE_ADDR_FLAGS,
		.reg_en_ctrl2 = MT6360_REG_LDO7_EN_CTRL2,
		.reg_ctrl3 = MT6360_REG_LDO7_CTRL3,
		.mask_vosel = MT6360_MASK_LDO7_VOSEL,
		.shift_vosel = MT6360_MASK_LDO7_VOSEL_SHIFT,
		.mask_vocal = MT6360_MASK_LDO7_VOCAL,
	},
	[MT6360_BUCK1] = {
		.name = "mt6360_buck1",
		.addr = MT6360_PMIC_SLAVE_ADDR_FLAGS,
		.reg_en_ctrl2 = MT6360_REG_BUCK1_EN_CTRL2,
		.reg_ctrl3 = MT6360_REG_BUCK1_VOSEL,
		.mask_vosel = MT6360_MASK_BUCK1_VOSEL,
		.shift_vosel = MT6360_MASK_BUCK1_VOSEL_SHIFT,
		.mask_vocal = MT6360_MASK_BUCK1_VOCAL,
	},
	[MT6360_BUCK2] = {
		.name = "mt6360_buck2",
		.addr = MT6360_PMIC_SLAVE_ADDR_FLAGS,
		.reg_en_ctrl2 = MT6360_REG_BUCK2_EN_CTRL2,
		.reg_ctrl3 = MT6360_REG_BUCK2_VOSEL,
		.mask_vosel = MT6360_MASK_BUCK2_VOSEL,
		.shift_vosel = MT6360_MASK_BUCK2_VOSEL_SHIFT,
		.mask_vocal = MT6360_MASK_BUCK2_VOCAL,
	},
};

static bool is_buck_regulator(const struct mt6360_regulator_data *data)
{
	/* There's no ldo_vosel_table, it's a buck. */
	return !(data->ldo_vosel_table);
}

int mt6360_regulator_get_info(enum mt6360_regulator_id id, char *name,
			      uint16_t *num_voltages, uint16_t *voltages_mv)
{
	int i;
	int cnt = 0;
	const struct mt6360_regulator_data *data;

	if (id >= MT6360_REGULATOR_COUNT)
		return EC_ERROR_INVAL;
	data = &regulator_data[id];

	strzcpy(name, data->name, EC_REGULATOR_NAME_MAX_LEN);

	if (is_buck_regulator(data)) {
		for (i = 0; i < MT6360_BUCK_VOSEL_MAX_STEP; ++i) {
			int mv = MT6360_BUCK_VOSEL_MIN +
				 i * MT6360_BUCK_VOSEL_STEP_MV;

			if (cnt < EC_REGULATOR_VOLTAGE_MAX_COUNT)
				voltages_mv[cnt++] = mv;
			else
				CPRINTS("%s voltage info overflow: %d-%d",
					data->name, mv, MT6360_BUCK_VOSEL_MAX);
		}
	} else {
		/* It's a LDO */
		for (i = 0; i < data->ldo_vosel_table_len; i++) {
			int mv = data->ldo_vosel_table[i];

			if (!mv)
				continue;
			if (cnt < EC_REGULATOR_VOLTAGE_MAX_COUNT)
				voltages_mv[cnt++] = mv;
			else
				CPRINTS("%s voltage info overflow: %d",
					data->name, mv);
		}
	}

	*num_voltages = cnt;
	return EC_SUCCESS;
}

int mt6360_regulator_enable(enum mt6360_regulator_id id, uint8_t enable)
{
	const struct mt6360_regulator_data *data;

	if (id >= MT6360_REGULATOR_COUNT)
		return EC_ERROR_INVAL;
	data = &regulator_data[id];

	if (enable)
		return mt6360_regulator_update_bits(
			data->addr,
			data->reg_en_ctrl2,
			MT6360_MASK_RGL_SW_OP_EN | MT6360_MASK_RGL_SW_EN,
			MT6360_MASK_RGL_SW_OP_EN | MT6360_MASK_RGL_SW_EN);
	else
		return mt6360_regulator_update_bits(
			data->addr,
			data->reg_en_ctrl2,
			MT6360_MASK_RGL_SW_OP_EN | MT6360_MASK_RGL_SW_EN,
			MT6360_MASK_RGL_SW_OP_EN);
}

int mt6360_regulator_is_enabled(enum mt6360_regulator_id id, uint8_t *enabled)
{
	int rv;
	int value;
	const struct mt6360_regulator_data *data;

	if (id >= MT6360_REGULATOR_COUNT)
		return EC_ERROR_INVAL;
	data = &regulator_data[id];

	rv = mt6360_regulator_read8(data->addr, data->reg_en_ctrl2, &value);
	if (rv) {
		CPRINTS("Error reading %s enabled: %d", data->name, rv);
		return rv;
	}
	*enabled = !!(value & MT6360_MASK_RGL_SW_EN);
	return EC_SUCCESS;
}

int mt6360_regulator_set_voltage(enum mt6360_regulator_id id, int min_mv,
				 int max_mv)
{
	int i;
	const struct mt6360_regulator_data *data;

	if (id >= MT6360_REGULATOR_COUNT)
		return EC_ERROR_INVAL;
	data = &regulator_data[id];

	if (is_buck_regulator(data)) {
		int mv;
		int step;

		if (max_mv < MT6360_BUCK_VOSEL_MIN)
			goto error;

		if (min_mv > MT6360_BUCK_VOSEL_MAX)
			goto error;

		mv = DIV_ROUND_UP((min_mv + max_mv) / 2,
				  MT6360_BUCK_VOSEL_STEP_MV) *
		     MT6360_BUCK_VOSEL_STEP_MV;
		mv = MIN(MAX(mv, MT6360_BUCK_VOSEL_MIN), MT6360_BUCK_VOSEL_MAX);

		step = (mv - MT6360_BUCK_VOSEL_MIN) / MT6360_BUCK_VOSEL_STEP_MV;

		return mt6360_regulator_update_bits(data->addr,
						    data->reg_ctrl3,
						    data->mask_vosel, step);
	}

	/* It's a LDO. */
	for (i = 0; i < data->ldo_vosel_table_len; i++) {
		int mv = data->ldo_vosel_table[i];
		int step = 0;

		if (!mv)
			continue;
		if (mv + MT6360_LDO_VOCAL_STEP_MV * MT6360_LDO_VOCAL_MAX_STEP <
		    min_mv)
			continue;
		if (mv < min_mv)
			step = DIV_ROUND_UP(min_mv - mv,
					    MT6360_LDO_VOCAL_STEP_MV);
		if (mv + step * MT6360_LDO_VOCAL_STEP_MV > max_mv)
			continue;
		return mt6360_regulator_update_bits(
			data->addr,
			data->reg_ctrl3,
			data->mask_vosel | data->mask_vocal,
			(i << data->shift_vosel) | step);
	}

error:
	CPRINTS("%s voltage %d - %d out of range", data->name, min_mv, max_mv);
	return EC_ERROR_INVAL;
}

int mt6360_regulator_get_voltage(enum mt6360_regulator_id id, int *voltage_mv)
{
	int value;
	int rv;
	const struct mt6360_regulator_data *data;

	if (id >= MT6360_REGULATOR_COUNT)
		return EC_ERROR_INVAL;
	data = &regulator_data[id];

	rv = mt6360_regulator_read8(data->addr, data->reg_ctrl3, &value);
	if (rv) {
		CPRINTS("Error reading %s ctrl3: %d", data->name, rv);
		return rv;
	}

	/* BUCK */
	if (is_buck_regulator(data)) {
		*voltage_mv = MT6360_BUCK_VOSEL_MIN +
			      value * MT6360_BUCK_VOSEL_STEP_MV;
		return EC_SUCCESS;
	}

	/* LDO */
	*voltage_mv = data->ldo_vosel_table[(value & data->mask_vosel) >>
					    data->shift_vosel];
	if (*voltage_mv == 0) {
		CPRINTS("Unknown %s voltage value: %d", data->name, value);
		return EC_ERROR_INVAL;
	}
	*voltage_mv +=
		MIN(MT6360_LDO_VOCAL_MAX_STEP, value & data->mask_vocal) *
		MT6360_LDO_VOCAL_STEP_MV;
	return EC_SUCCESS;
}

/* RGB LED */
void mt6360_led_init(void)
{
	/* Enable LED1 software mode */
	mt6360_set_bit(MT6360_REG_RGB_EN, MT6360_ISINK1_CHRIND_EN_SEL);
}
DECLARE_HOOK(HOOK_INIT, mt6360_led_init, HOOK_PRIO_DEFAULT);

int mt6360_led_enable(enum mt6360_led_id led_id, int enable)
{
	if (!IN_RANGE(led_id, 0, MT6360_LED_COUNT))
		return EC_ERROR_INVAL;

	if (enable)
		return mt6360_set_bit(MT6360_REG_RGB_EN,
				      MT6360_MASK_ISINK_EN(led_id));
	return mt6360_clr_bit(MT6360_REG_RGB_EN, MT6360_MASK_ISINK_EN(led_id));
}

int mt6360_led_set_brightness(enum mt6360_led_id led_id, int brightness)
{
	int val;

	if (!IN_RANGE(led_id, 0, MT6360_LED_COUNT))
		return EC_ERROR_INVAL;
	if (!IN_RANGE(brightness, 0, 16))
		return EC_ERROR_INVAL;

	RETURN_ERROR(mt6360_read8(MT6360_REG_RGB_ISINK(led_id), &val));
	val &= ~MT6360_MASK_CUR_SEL;
	val |= brightness;

	return mt6360_write8(MT6360_REG_RGB_ISINK(led_id), val);
}

const struct bc12_drv mt6360_drv = {
	.usb_charger_task = mt6360_usb_charger_task,
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &mt6360_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
