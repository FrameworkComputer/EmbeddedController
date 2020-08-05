/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MT6360_H

#define MT6360_PMU_SLAVE_ADDR_FLAGS 0x34
#define MT6360_PMIC_SLAVE_ADDR_FLAGS 0x1A
#define MT6360_LDO_SLAVE_ADDR_FLAGS 0x64
#define MT6360_PD_SLAVE_ADDR_FLAGS 0x4E

#define MT6360_IRQ_MASK 0x0C

#define MT6360_REG_DEVICE_TYPE 0x22
#define MT6360_MASK_USBCHGEN BIT(7)

#define MT6360_REG_USB_STATUS_1 0x27
#define MT6360_MASK_USB_STATUS 0x70
#define MT6360_MASK_SDP 0x20
#define MT6360_MASK_DCP 0x40
#define MT6360_MASK_CDP 0x50

#define MT6360_REG_RGB_EN 0x80
#define MT6360_MASK_ISINK_EN(x) BIT(7 - (x))
#define MT6360_ISINK1_CHRIND_EN_SEL BIT(3)

#define MT6360_REG_RGB_ISINK(x) (0x81 + (x))
#define MT6360_MASK_CUR_SEL 0xF

#define MT6360_REG_DPDMIRQ 0xD6
#define MT6360_MASK_DPDMIRQ_ATTACH BIT(0)
#define MT6360_MASK_DPDMIRQ_DETACH BIT(1)

#define MT6360_REG_DPDM_MASK1 0xF6
#define MT6360_REG_DPDM_MASK1_CHGDET_DONEI_M BIT(0)

#define MT6360_REG_LDO3_EN_CTRL2 0x05

#define MT6360_REG_LDO3_CTRL3 0x09
#define MT6360_MASK_LDO3_VOSEL 0xF0
#define MT6360_MASK_LDO3_VOSEL_SHIFT 4
#define MT6360_MASK_LDO3_VOCAL 0x0F

#define MT6360_REG_LDO5_EN_CTRL2 0x0B

#define MT6360_REG_LDO5_CTRL3 0x0F
#define MT6360_MASK_LDO5_VOSEL 0x70
#define MT6360_MASK_LDO5_VOSEL_SHIFT 4
#define MT6360_MASK_LDO5_VOCAL 0x0F

#define MT6360_REG_LDO6_EN_CTRL2 0x37

#define MT6360_REG_LDO6_CTRL3 0x3B
#define MT6360_MASK_LDO6_VOSEL 0xF0
#define MT6360_MASK_LDO6_VOSEL_SHIFT 4
#define MT6360_MASK_LDO6_VOCAL 0x0F

#define MT6360_REG_LDO7_EN_CTRL2 0x31

#define MT6360_REG_LDO7_CTRL3 0x35
#define MT6360_MASK_LDO7_VOSEL 0xF0
#define MT6360_MASK_LDO7_VOSEL_SHIFT 4
#define MT6360_MASK_LDO7_VOCAL 0x0F

#define MT6360_REG_BUCK1_EN_CTRL2 0x17

#define MT6360_REG_BUCK1_VOSEL 0x10
#define MT6360_MASK_BUCK1_VOSEL 0xFF
#define MT6360_MASK_BUCK1_VOSEL_SHIFT 0
#define MT6360_MASK_BUCK1_VOCAL 0x0

#define MT6360_REG_BUCK2_EN_CTRL2 0x26

#define MT6360_REG_BUCK2_VOSEL 0x20
#define MT6360_MASK_BUCK2_VOSEL 0xFF
#define MT6360_MASK_BUCK2_VOSEL_SHIFT 0
#define MT6360_MASK_BUCK2_VOCAL 0x0

/* This is same for LDO{1,2,3,5,6,7}_EN_CTRL2, BUCK{1,2}_EN_CTRL2 */
#define MT6360_MASK_RGL_SW_OP_EN BIT(7)
#define MT6360_MASK_RGL_SW_EN BIT(6)

#define MT6360_LDO_VOCAL_STEP_MV 10
#define MT6360_LDO_VOCAL_MAX_STEP 10

#define MT6360_BUCK_VOSEL_STEP_MV 5
#define MT6360_BUCK_VOSEL_MAX_STEP 200
#define MT6360_BUCK_VOSEL_MIN 300
#define MT6360_BUCK_VOSEL_MAX    \
	(MT6360_BUCK_VOSEL_MIN + \
	 MT6360_BUCK_VOSEL_STEP_MV * MT6360_BUCK_VOSEL_MAX_STEP)

enum mt6360_regulator_id {
	MT6360_LDO3,
	MT6360_LDO5,
	MT6360_LDO6,
	MT6360_LDO7,
	MT6360_BUCK1,
	MT6360_BUCK2,

	MT6360_REGULATOR_COUNT,
};

int mt6360_regulator_get_info(enum mt6360_regulator_id id, char *name,
			      uint16_t *voltage_count, uint16_t *voltages_mv);

int mt6360_regulator_enable(enum mt6360_regulator_id id, uint8_t enable);

int mt6360_regulator_is_enabled(enum mt6360_regulator_id id, uint8_t *enabled);

int mt6360_regulator_set_voltage(enum mt6360_regulator_id id, int min_mv,
				 int max_mv);

int mt6360_regulator_get_voltage(enum mt6360_regulator_id id, int *voltage_mv);

enum mt6360_led_id {
	MT6360_LED_RGB1,
	MT6360_LED_RGB2,
	MT6360_LED_RGB3,
	MT6360_LED_RGB_ML,

	MT6360_LED_COUNT,
};

#define MT6360_LED_BRIGHTNESS_MAX 15

struct mt6360_config_t {
	int i2c_port;
	int i2c_addr_flags;
};

int mt6360_led_enable(enum mt6360_led_id led_id, int enable);

int mt6360_led_set_brightness(enum mt6360_led_id led_id, int brightness);

extern const struct mt6360_config_t mt6360_config;

extern const struct bc12_drv mt6360_drv;

#endif /* __CROS_EC_MT6360_H */
