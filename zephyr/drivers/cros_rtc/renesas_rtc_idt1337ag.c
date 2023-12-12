/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT renesas_rtc_idt1337ag

#include "renesas_rtc_idt1337ag.h"

#include <assert.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_rtc.h>
#include <rtc.h>
#include <soc.h>
LOG_MODULE_REGISTER(cros_rtc, LOG_LEVEL_ERR);

/* Driver config */
struct renesas_rtc_idt1337ag_config {
	const struct device *bus;
	const uint16_t i2c_addr_flags;
	const struct gpio_dt_spec gpio_alert;
};

/* Driver data */
struct renesas_rtc_idt1337ag_data {
	const struct device *dev;
	struct gpio_callback gpio_cb;
	cros_rtc_alarm_callback_t alarm_callback;
};

enum timer_type {
	RTC_TIMER,
	ALARM_TIMER,
};

/*
 * type == ALARM_TIMER: Reads alarm registers SECONDS, MINUTES, HOURS, and DAYS
 * type == RTC_TIMER: Reads time registers SECONDS, MINUTES, HOURS, DAYS, and
 *			MONTHS, YEARS
 */
static int idt1337ag_read_time_regs(const struct device *dev, uint8_t *time_reg,
				    enum timer_type type)
{
	const struct renesas_rtc_idt1337ag_config *const config = dev->config;
	uint8_t start_reg;
	uint8_t num_reg;

	if (type == ALARM_TIMER) {
		start_reg = REG_SECOND_ALARM1;
		num_reg = NUM_ALARM_REGS;
	} else {
		start_reg = REG_SECONDS;
		num_reg = NUM_TIMER_REGS;
	}

	return i2c_burst_read(config->bus, config->i2c_addr_flags, start_reg,
			      time_reg, num_reg);
}

static int idt1337ag_read_reg(const struct device *dev, uint8_t reg,
			      uint8_t *val)
{
	const struct renesas_rtc_idt1337ag_config *const config = dev->config;

	return i2c_reg_read_byte(config->bus, config->i2c_addr_flags, reg, val);
}

/*
 * type == ALARM_TIMER: Writes alarm registers SECONDS, MINUTES, HOURS, and DAYS
 * type == RTC_TIMER: Writes time registers SECONDS, MINUTES, HOURS, DAYS, and
 *			MONTHS, YEARS
 */
static int idt1337ag_write_time_regs(const struct device *dev,
				     uint8_t *time_reg, enum timer_type type)
{
	const struct renesas_rtc_idt1337ag_config *const config = dev->config;
	uint8_t start_reg;
	uint8_t num_reg;

	if (type == ALARM_TIMER) {
		/*
		 * Register 0x0A bit 6 determines if the DAY(1b) or DATE(0b)
		 * alarm is selected.
		 * Select the DAY alarm
		 */
		time_reg[DAYS] |= SELECT_DAYS_ALARM;

		start_reg = REG_SECOND_ALARM1;
		num_reg = NUM_ALARM_REGS;
	} else {
		start_reg = REG_SECONDS;
		num_reg = NUM_TIMER_REGS;
	}

	return i2c_burst_write(config->bus, config->i2c_addr_flags, start_reg,
			       time_reg, num_reg);
}

static int idt1337ag_write_reg(const struct device *dev, uint8_t reg,
			       uint8_t val)
{
	const struct renesas_rtc_idt1337ag_config *const config = dev->config;
	uint8_t tx_buf[2];

	tx_buf[0] = reg;
	tx_buf[1] = val;

	return i2c_write(config->bus, tx_buf, sizeof(tx_buf),
			 config->i2c_addr_flags);
}

/*
 * val bits 7 to 4 - tens place
 * val bits 3 to 0 - ones place
 */
static int bcd_to_dec(uint8_t val, enum bcd_mask mask)
{
	int tens = ((val & mask) >> 4) * 10;
	int ones = (val & 0xf);

	return tens + ones;
}

/*
 * val bits 7 to 4 - tens place
 * val bits 3 to 0 - ones place
 */
static uint8_t dec_to_bcd(uint32_t val, enum bcd_mask mask)
{
	int tens = val / 10;
	int ones = val - (tens * 10);

	return ((tens << 4) & mask) | ones;
}

static int renesas_rtc_idt1337ag_read_seconds(const struct device *dev,
					      uint32_t *value,
					      enum timer_type type)
{
	uint8_t time_reg[NUM_TIMER_REGS];
	struct calendar_date time;
	int ret;

	ret = idt1337ag_read_time_regs(dev, time_reg, type);

	if (ret < 0) {
		return ret;
	}

	if (type == ALARM_TIMER) {
		*value =
			(bcd_to_dec(time_reg[DAYS], DAYS_MASK) * SECS_PER_DAY) +
			(bcd_to_dec(time_reg[HOURS], HOURS24_MASK) *
			 SECS_PER_HOUR) +
			(bcd_to_dec(time_reg[MINUTES], MINUTES_MASK) *
			 SECS_PER_MINUTE) +
			bcd_to_dec(time_reg[SECONDS], SECONDS_MASK);
	} else {
		time.year = bcd_to_dec(time_reg[YEARS], YEARS_MASK);
		time.month = bcd_to_dec(time_reg[MONTHS], MONTHS_MASK);
		time.day = bcd_to_dec(time_reg[DAYS], DAYS_MASK);

		*value = date_to_sec(time) - SECS_TILL_YEAR_2K +
			 (bcd_to_dec(time_reg[HOURS], HOURS24_MASK) *
			  SECS_PER_HOUR) +
			 (bcd_to_dec(time_reg[MINUTES], MINUTES_MASK) *
			  SECS_PER_MINUTE) +
			 bcd_to_dec(time_reg[SECONDS], SECONDS_MASK);
	}

	return ret;
}

static int renesas_rtc_idt1337ag_write_seconds(const struct device *dev,
					       uint32_t value,
					       enum timer_type type)
{
	uint8_t time_reg[NUM_TIMER_REGS];
	struct calendar_date time;
	uint32_t tmp_sec;

	time = sec_to_date(value + SECS_TILL_YEAR_2K);

	if (type == RTC_TIMER) {
		time_reg[YEARS] = dec_to_bcd(time.year, YEARS_MASK);
		time_reg[MONTHS] = dec_to_bcd(time.month, MONTHS_MASK);
	}

	time_reg[DAYS] = dec_to_bcd(time.day, DAYS_MASK);

	value %= SECS_PER_DAY;
	tmp_sec = value / SECS_PER_HOUR;
	time_reg[HOURS] = dec_to_bcd(tmp_sec, HOURS24_MASK);

	value -= (tmp_sec * SECS_PER_HOUR);
	tmp_sec = value / SECS_PER_MINUTE;
	time_reg[MINUTES] = dec_to_bcd(tmp_sec, MINUTES_MASK);

	value -= (tmp_sec * SECS_PER_MINUTE);
	time_reg[SECONDS] = dec_to_bcd(value, SECONDS_MASK);

	return idt1337ag_write_time_regs(dev, time_reg, type);
}

static int renesas_rtc_idt1337ag_configure(const struct device *dev,
					   cros_rtc_alarm_callback_t callback)
{
	struct renesas_rtc_idt1337ag_data *data = dev->data;

	if (callback == NULL) {
		return -EINVAL;
	}

	data->alarm_callback = callback;

	return 0;
}

static int renesas_rtc_idt1337ag_get_value(const struct device *dev,
					   uint32_t *value)
{
	return renesas_rtc_idt1337ag_read_seconds(dev, value, RTC_TIMER);
}

static int renesas_rtc_idt1337ag_set_value(const struct device *dev,
					   uint32_t value)
{
	return renesas_rtc_idt1337ag_write_seconds(dev, value, RTC_TIMER);
}

static int renesas_rtc_idt1337ag_get_alarm(const struct device *dev,
					   uint32_t *seconds,
					   uint32_t *microseconds)
{
	*microseconds = 0;
	return renesas_rtc_idt1337ag_read_seconds(dev, seconds, ALARM_TIMER);
}

static int renesas_rtc_idt1337ag_reset_alarm(const struct device *dev)
{
	uint8_t time_reg[NUM_ALARM_REGS];
	int ret;
	uint8_t val;

	ret = idt1337ag_read_reg(dev, REG_CONTROL, &val);

	if (ret < 0) {
		return ret;
	}

	/* Disable alarm interrupt and clear pending alarm flag */
	val &= ~CONTROL_A1IE;
	ret = idt1337ag_write_reg(dev, REG_CONTROL, val);

	if (ret < 0) {
		return ret;
	}

	/* Clear alarm1 flag if set */
	ret = idt1337ag_read_reg(dev, REG_STATUS, &val);

	if (ret < 0) {
		return ret;
	}

	/* Clear the alarm1 and alarm2 flag */
	val &= ~(STATUS_A1F | STATUS_A2F);
	ret = idt1337ag_write_reg(dev, REG_STATUS, val);

	if (ret < 0) {
		return ret;
	}

	/* Clear and disable the alarm registers */
	time_reg[SECONDS] = DISABLE_ALARM;
	time_reg[MINUTES] = DISABLE_ALARM;
	time_reg[HOURS] = DISABLE_ALARM;
	time_reg[DAYS] = DISABLE_ALARM;

	return idt1337ag_write_time_regs(dev, time_reg, ALARM_TIMER);
}

static int renesas_rtc_idt1337ag_set_alarm(const struct device *dev,
					   uint32_t seconds,
					   uint32_t microseconds)
{
	int ret;
	uint8_t val;

	ARG_UNUSED(microseconds);

	ret = renesas_rtc_idt1337ag_reset_alarm(dev);
	if (ret < 0) {
		return ret;
	}

	ret = renesas_rtc_idt1337ag_write_seconds(dev, seconds, ALARM_TIMER);
	if (ret < 0) {
		return ret;
	}

	ret = idt1337ag_read_reg(dev, REG_CONTROL, &val);
	if (ret < 0) {
		return ret;
	}

	val |= CONTROL_A1IE;
	idt1337ag_write_reg(dev, REG_CONTROL, val);

	return 0;
}

static void renesas_rtc_idt1337ag_isr(const struct device *port,
				      struct gpio_callback *cb, uint32_t pin)
{
	struct renesas_rtc_idt1337ag_data *data =
		CONTAINER_OF(cb, struct renesas_rtc_idt1337ag_data, gpio_cb);
	const struct device *dev = data->dev;

	ARG_UNUSED(port);
	ARG_UNUSED(pin);
	ARG_UNUSED(cb);

	LOG_DBG("%s", __func__);

	/* Call callback function */
	if (data->alarm_callback) {
		data->alarm_callback(dev);
	}
}

static const struct cros_rtc_driver_api renesas_rtc_idt1337ag_driver_api = {
	.configure = renesas_rtc_idt1337ag_configure,
	.get_value = renesas_rtc_idt1337ag_get_value,
	.set_value = renesas_rtc_idt1337ag_set_value,
	.get_alarm = renesas_rtc_idt1337ag_get_alarm,
	.set_alarm = renesas_rtc_idt1337ag_set_alarm,
	.reset_alarm = renesas_rtc_idt1337ag_reset_alarm,
};

static int renesas_rtc_idt1337ag_init(const struct device *dev)
{
	const struct renesas_rtc_idt1337ag_config *const config = dev->config;
	struct renesas_rtc_idt1337ag_data *data = dev->data;
	uint8_t val;
	int ret;

	if (!device_is_ready(config->bus)) {
		LOG_ERR("device %s not ready", config->bus->name);
		return -ENODEV;
	}

	/*
	 * Read Control register. For normal operation,
	 * the values should be as follows:
	 *	Bit 7 (enable oscillator) : (0) normal mode
	 *	Bit 6 (unused)            : (0)
	 *	Bit 5 (unused)            : (0)
	 *	BIT 4 (RS2)               : (0) Not used when INTCN == 1
	 *	BIT 3 (RS1)               : (0) Not used when INTCN == 1
	 *	BIT 2 (INTCN)             : (1) a match between the timekeeping
	 *	                                registers and the alarm 1
	 *	                                registers activate the INTA pin
	 *	BIT 1 (A2IE)              : (0) Alarm 2 is not used
	 *	BIT 0 (A1IE)              : (1) Enables Alarm 1
	 */
	ret = idt1337ag_read_reg(dev, REG_CONTROL, &val);

	if (ret < 0) {
		return ret;
	}

	/* Enable IDT1337AG oscillator */
	val &= ~CONTROL_EOSC;

	/* Disable Alarm 2 */
	val &= ~CONTROL_A2IE;

	/* Alarm 1 assert INTA pin */
	val |= CONTROL_INTCN;

	ret = idt1337ag_write_reg(dev, REG_CONTROL, val);

	if (ret < 0) {
		return ret;
	}

	/* Date register isn't used. Set it to zero */
	ret = idt1337ag_write_reg(dev, REG_DATE, 0);

	/* Make sure the oscillator is running */
	ret = idt1337ag_read_reg(dev, REG_STATUS, &val);

	if (ret < 0) {
		return ret;
	}

	/* Clear IDT1337AG oscillator not running flag */
	val &= ~STATUS_OSF;

	/* Clear Alarm 2 flag */
	val &= ~STATUS_A2F;

	ret = idt1337ag_write_reg(dev, REG_STATUS, val);

	if (ret < 0) {
		return ret;
	}

	renesas_rtc_idt1337ag_reset_alarm(dev);

	/* Disable Alarm2 */
	idt1337ag_write_reg(dev, REG_MINUTE_ALARM2, DISABLE_ALARM);
	idt1337ag_write_reg(dev, REG_HOUR_ALARM2, DISABLE_ALARM);
	idt1337ag_write_reg(dev, REG_DAY_ALARM2, DISABLE_ALARM);

	/* Configure GPIO interrupt pin for IDT1337AG alarm pin */

	if (!device_is_ready(config->gpio_alert.port)) {
		LOG_ERR("Alert GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->gpio_alert, GPIO_INPUT);

	if (ret < 0) {
		LOG_ERR("Could not configure RTC alert pin");
		return ret;
	}

	gpio_init_callback(&data->gpio_cb, renesas_rtc_idt1337ag_isr,
			   BIT(config->gpio_alert.pin));

	ret = gpio_add_callback(config->gpio_alert.port, &data->gpio_cb);

	if (ret < 0) {
		LOG_ERR("Could not set RTC alert pin callback");
		return ret;
	}

	data->dev = dev;

	return gpio_pin_interrupt_configure_dt(&config->gpio_alert,
					       GPIO_INT_EDGE_FALLING);
}

#define IDT1337AG_INT_PIN DT_PHANDLE(DT_NODELABEL(idt1337ag), int_pin)

/*
 * dt_flags is a uint8_t type.  However, for platform/ec
 * the GPIO flags in the devicetree are expanded past 8 bits
 * to support the INPUT/OUTPUT and PULLUP/PULLDOWN properties.
 * Cast back to a gpio_dt_flags to compile, discarding the bits
 * that are not supported by the Zephyr GPIO API.
 */
#define CROS_EC_GPIO_DT_SPEC_GET(node_id, prop)                            \
	{                                                                  \
		.port = DEVICE_DT_GET(DT_GPIO_CTLR(node_id, prop)),        \
		.pin = DT_GPIO_PIN(node_id, prop),                         \
		.dt_flags = (gpio_dt_flags_t)DT_GPIO_FLAGS(node_id, prop), \
	}

static const struct renesas_rtc_idt1337ag_config renesas_rtc_idt1337ag_cfg_0 = {
	.bus = DEVICE_DT_GET(DT_INST_BUS(0)),
	.i2c_addr_flags = DT_INST_REG_ADDR(0),
	.gpio_alert = CROS_EC_GPIO_DT_SPEC_GET(IDT1337AG_INT_PIN, gpios)
};

static struct renesas_rtc_idt1337ag_data renesas_rtc_idt1337ag_data_0;

DEVICE_DT_INST_DEFINE(0, renesas_rtc_idt1337ag_init, /* pm_control_fn= */ NULL,
		      &renesas_rtc_idt1337ag_data_0,
		      &renesas_rtc_idt1337ag_cfg_0, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		      &renesas_rtc_idt1337ag_driver_api);
