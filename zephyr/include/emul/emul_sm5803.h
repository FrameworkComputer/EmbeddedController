/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>

const struct gpio_dt_spec *
sm5803_emul_get_interrupt_gpio(const struct emul *emul);
struct i2c_common_emul_data *sm5803_emul_get_i2c_main(const struct emul *emul);
struct i2c_common_emul_data *sm5803_emul_get_i2c_chg(const struct emul *emul);
struct i2c_common_emul_data *sm5803_emul_get_i2c_meas(const struct emul *emul);
struct i2c_common_emul_data *sm5803_emul_get_i2c_test(const struct emul *emul);

/**
 * Read the value of a charger page register, by address.
 *
 * This is useful to verify that a user has written an expected value to a
 * register, without depending on the user's corresponding getter function.
 *
 * @return negative value on error, otherwise 8-bit register value.
 */
int sm5803_emul_read_chg_reg(const struct emul *emul, uint8_t reg);

/**
 * Set the reported VBUS voltage, in mV.
 *
 * If the VBUS voltage crosses the charger detection threshold as a result,
 * a CHG_DET interrupt will automatically be triggered.
 */
void sm5803_emul_set_vbus_voltage(const struct emul *emul, uint16_t mv);

/** Set the reported input current (from VBUS), in mA. */
void sm5803_emul_set_input_current(const struct emul *emul, uint16_t mv);

/** Set the reported battery charge current, in mA. */
void sm5803_emul_set_battery_current(const struct emul *emul, uint16_t ma);

/** Set the reported device ID (default 3). */
void sm5803_emul_set_device_id(const struct emul *emul, uint8_t id);

/** Set the platform ID as configured in hardware by the PMODE resistor. */
void sm5803_emul_set_pmode(const struct emul *emul, uint8_t pmode);

/** Get the register value of ICHG_FAST_SET; the fast charge current limit. */
int sm5803_emul_get_fast_charge_current_limit(const struct emul *emul);

/** Get the values of the GPADC_CONFIG_1 and GPADC_CONFIG_2 registers. */
void sm5803_emul_get_gpadc_conf(const struct emul *emul, uint8_t *conf1,
				uint8_t *conf2);

/** Set the GPADC enable bits in GPADC_CONFIG_1 and GPADC_CONFIG_2 registers. */
void sm5803_emul_set_gpadc_conf(const struct emul *emul, uint8_t conf1,
				uint8_t conf2);

/** Return whether the main clock is slowed (CLOCK_SEL:LOW_POWER_CLOCK_EN). */
bool sm5803_emul_is_clock_slowed(const struct emul *emul);

/** Get the value of the CC_CONFIG_1 register. */
uint8_t sm5803_emul_get_cc_config(const struct emul *emul);

/** Get the values of the FLOW1..FLOW3 registers. */
void sm5803_emul_get_flow_regs(const struct emul *emul, uint8_t *flow1,
			       uint8_t *flow2, uint8_t *flow3);

/**
 * Set the INT_REQ_* registers to indicate pending interrupts.
 *
 * This does not clear pending IRQs; it only asserts them. IRQs are cleared only
 * when the interrupt status registers are read.
 */
void sm5803_emul_set_irqs(const struct emul *emul, uint8_t irq1, uint8_t irq2,
			  uint8_t irq3, uint8_t irq4);

/** Get the value of the GPIO_CTRL_1 register, which controls GPIO0. */
uint8_t sm5803_emul_get_gpio_ctrl(const struct emul *emul);

/**
 * Get the values of the IR_COMP1 and IR_COMP2 registers.
 *
 * Register values are concatenated, with COMP1 as MSB and COMP2 as LSB.
 */
uint16_t sm5803_emul_get_ir_comp(const struct emul *emul);
