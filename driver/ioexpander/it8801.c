/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "it8801.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"
#include "util.h"
#include "keyboard_backlight.h"

#define CPRINTS(format, args...) cprints(CC_KEYSCAN, format, ## args)

static int it8801_ioex_set_level(int ioex, int port, int mask, int value);

static int it8801_read(int reg, int *data)
{
	return i2c_read8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_I2C_ADDR,
		reg, data);
}

static int it8801_write(int reg, int data)
{
	return i2c_write8(I2C_PORT_IO_EXPANDER_IT8801, IT8801_I2C_ADDR,
		reg, data);
}

struct it8801_vendor_id_t {
	uint8_t chip_id;
	uint8_t reg;
};

static const struct it8801_vendor_id_t it8801_vendor_id_verify[] = {
	{ 0x12, IT8801_REG_HBVIDR},
	{ 0x83, IT8801_REG_LBVIDR},
};

static int it8801_check_vendor_id(void)
{
	int i, ret, val;

	/*  Verify vendor ID registers(16-bits). */
	for (i = 0; i < ARRAY_SIZE(it8801_vendor_id_verify); i++) {
		ret = it8801_read(it8801_vendor_id_verify[i].reg, &val);

		if (ret != EC_SUCCESS)
			return ret;

		if (val != it8801_vendor_id_verify[i].chip_id)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void keyboard_raw_init(void)
{
	int ret;

	/*  Verify Vendor ID registers. */
	ret = it8801_check_vendor_id();
	if (ret) {
		CPRINTS("Failed to read IT8801 vendor id %x", ret);
		return;
	}

	/* KSO alternate function switching(KSO[21:20, 18]). */
	it8801_write(IT8801_REG_GPIO01_KSO18, IT8801_REG_MASK_GPIOAFS_FUNC2);
	it8801_write(IT8801_REG_GPIO22_KSO21, IT8801_REG_MASK_GPIOAFS_FUNC2);
	it8801_write(IT8801_REG_GPIO23_KSO20, IT8801_REG_MASK_GPIOAFS_FUNC2);

	/* Start with KEYBOARD_COLUMN_ALL, KSO[22:11, 6:0] output low. */
	it8801_write(IT8801_REG_KSOMCR, IT8801_REG_MASK_AKSOSC);

	if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
		/*
		 * Since most of the KSO pins can't drive up, we'll must use
		 * a pin capable of being a GPIO instead and use the GPIO
		 * feature to do the required inverted push pull.
		 */
		it8801_write(IT8801_REG_GPIO23_KSO20, IT8801_REG_MASK_GPIODIR);

		/* Start with KEYBOARD_COLUMN_ALL, output high(so selected). */
		it8801_ioex_set_level(0, 2, IT8801_REG_GPIO23SOV, 1);
	}

	/* Keyboard scan in interrupt enable register */
	it8801_write(IT8801_REG_KSIIER, 0xff);
	/* Gather KSI interrupt enable */
	it8801_write(IT8801_REG_GIECR, IT8801_REG_MASK_GKSIIE);
	/* Alert response enable */
	it8801_write(IT8801_REG_SMBCR, IT8801_REG_MASK_ARE);

	keyboard_raw_enable_interrupt(0);
}

void keyboard_raw_task_start(void)
{
	keyboard_raw_enable_interrupt(1);
}

static const uint8_t kso_mapping[] = {
	0, 1, 20, 3, 4, 5, 6, 17, 18, 16, 15, 11, 12,
#ifdef CONFIG_KEYBOARD_KEYPAD
	13, 14
#endif
};
BUILD_ASSERT(ARRAY_SIZE(kso_mapping) == KEYBOARD_COLS_MAX);

test_mockable void keyboard_raw_drive_column(int col)
{
	int kso_val;

	/* Tri-state all outputs */
	if (col == KEYBOARD_COLUMN_NONE) {
		/* KSO[22:11, 6:0] output high */
		kso_val = IT8801_REG_MASK_KSOSDIC | IT8801_REG_MASK_AKSOSC;

		if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
			/* Output low(so not selected). */
			it8801_ioex_set_level(0, 2, IT8801_REG_GPIO23SOV, 0);
		}
	}
	/* Assert all outputs */
	else if (col == KEYBOARD_COLUMN_ALL) {
		/* KSO[22:11, 6:0] output low */
		kso_val = IT8801_REG_MASK_AKSOSC;

		if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
			/* Output high(so selected). */
			it8801_ioex_set_level(0, 2, IT8801_REG_GPIO23SOV, 1);
		}
	} else {
		/* To check if column is valid or not. */
		if (col >= KEYBOARD_COLS_MAX)
			return;
		/*
		 * Selected KSO[20, 18:11, 6:3, 1:0] output low,
		 * all others KSO output high.
		 */
		kso_val = kso_mapping[col];

		if (IS_ENABLED(CONFIG_KEYBOARD_COL2_INVERTED)) {
			/* GPIO23 is inverted. */
			if (col == IT8801_REG_MASK_SELKSO2) {
				/* Output high(so selected). */
				it8801_ioex_set_level(0, 2,
						IT8801_REG_GPIO23SOV, 1);
			} else {
				/* Output low(so not selected). */
				it8801_ioex_set_level(0, 2,
						IT8801_REG_GPIO23SOV, 0);
			}
		}
	}

	it8801_write(IT8801_REG_KSOMCR, kso_val);
}

test_mockable int keyboard_raw_read_rows(void)
{
	int data = 0;
	int ksieer = 0;

	it8801_read(IT8801_REG_KSIDR, &data);

	/* This register needs to write clear after reading data */
	it8801_read(IT8801_REG_KSIEER, &ksieer);
	it8801_write(IT8801_REG_KSIEER, ksieer);

	/* Bits are active-low, so invert returned levels */
	return (~data) & 0xff;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		it8801_write(IT8801_REG_KSIEER, 0xff);
		gpio_clear_pending_interrupt(GPIO_IT8801_SMB_INT);
		gpio_enable_interrupt(GPIO_IT8801_SMB_INT);
	} else {
		gpio_disable_interrupt(GPIO_IT8801_SMB_INT);
	}
}

void io_expander_it8801_interrupt(enum gpio_signal signal)
{
	/* Wake the scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static int it8801_ioex_read(int ioex, int reg, int *data)
{
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	return i2c_read8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
			 reg, data);
}

static int it8801_ioex_write(int ioex, int reg, int data)
{
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	return i2c_write8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
			  reg, data);
}

static const int it8801_valid_gpio_group[] = {
	IT8801_VALID_GPIO_G0_MASK,
	IT8801_VALID_GPIO_G1_MASK,
	IT8801_VALID_GPIO_G2_MASK,
};

/* Mutexes */
static struct mutex ioex_mutex;

static uint8_t it8801_gpio_sov[ARRAY_SIZE(it8801_valid_gpio_group)];

/*
 * Initialize the general purpose I/O port(GPIO)
 */
static int it8801_ioex_init(int ioex)
{
	int ret, port, val = 0;

	/*  Verify Vendor ID registers. */
	ret = it8801_check_vendor_id();
	if (ret) {
		CPRINTS("Failed to read IT8801 vendor id %x", ret);
		return ret;
	}

	/*
	 * We will read the value of SOVR and write it to the
	 * cache(it8801_gpio_sov[port]) to avoid causing cache
	 * to reset when EC is reset.
	 */
	for (port = 0; port < ARRAY_SIZE(it8801_valid_gpio_group); port++) {
		it8801_ioex_read(ioex, IT8801_REG_GPIO_SOVR(port), &val);
		it8801_gpio_sov[port] = val;
	}

	return EC_SUCCESS;
}

static int ioex_check_is_not_valid(int port, int mask)
{
	if (port >= ARRAY_SIZE(it8801_valid_gpio_group)) {
		CPRINTS("Port%d is not support in IT8801", port);
		return EC_ERROR_INVAL;
	}

	if (mask & ~it8801_valid_gpio_group[port]) {
		CPRINTS("GPIO%d-%d is not support in IT8801", port,
			__fls(mask & ~it8801_valid_gpio_group[port]));
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static int it8801_ioex_get_level(int ioex, int port, int mask, int *val)
{
	int rv;

	if (ioex_check_is_not_valid(port, mask))
		return EC_ERROR_INVAL;

	rv = it8801_ioex_read(ioex, IT8801_REG_GPIO_IPSR(port), val);

	*val = !!(*val & mask);

	return rv;
}

static int it8801_ioex_set_level(int ioex, int port, int mask, int value)
{
	int rv = EC_SUCCESS;

	if (ioex_check_is_not_valid(port, mask))
		return EC_ERROR_INVAL;

	mutex_lock(&ioex_mutex);
	/*
	 * The bit of output value in SOV is different than
	 * the one we were about to set it to.
	 */
	if (!!(it8801_gpio_sov[port] & mask) ^ value) {
		if (value)
			it8801_gpio_sov[port] |= mask;
		else
			it8801_gpio_sov[port] &= ~mask;

		rv = it8801_ioex_write(ioex, IT8801_REG_GPIO_SOVR(port),
						it8801_gpio_sov[port]);
	}
	mutex_unlock(&ioex_mutex);

	return rv;
}

static int it8801_ioex_get_flags_by_mask(int ioex, int port,
		int mask, int *flags)
{
	int rv, val;

	if (ioex_check_is_not_valid(port, mask))
		return EC_ERROR_INVAL;

	rv = it8801_ioex_read(ioex, IT8801_REG_GPIO_CR(port, mask), &val);
	if (rv)
		return rv;

	*flags = 0;

	/* Get GPIO direction */
	*flags |= (val & IT8801_GPIODIR) ? GPIO_OUTPUT : GPIO_INPUT;

	/* Get GPIO type, 0:push-pull 1:open-drain */
	if (val & IT8801_GPIOIOT)
		*flags |= GPIO_OPEN_DRAIN;

	rv = it8801_ioex_read(ioex, IT8801_REG_GPIO_IPSR(port), &val);
	if (rv)
		return rv;

	/* Get GPIO output level */
	*flags |= (val & mask) ? GPIO_HIGH : GPIO_LOW;

	return EC_SUCCESS;
}

static int it8801_ioex_set_flags_by_mask(int ioex, int port,
		int mask, int flags)
{
	int rv, val;

	if (ioex_check_is_not_valid(port, mask))
		return EC_ERROR_INVAL;

	if (flags & ~IT8801_SUPPORT_GPIO_FLAGS) {
		CPRINTS("Flag 0x%08x is not supported at port %d, mask %d",
					flags, port, mask);
		return EC_ERROR_INVAL;
	}

	/* GPIO alternate function switching(GPIO[00, 12:15, 20:23]). */
	it8801_ioex_write(ioex, IT8801_REG_GPIO_CR(port, mask),
					IT8801_REG_MASK_GPIOAFS_FUNC1);

	mutex_lock(&ioex_mutex);
	rv = it8801_ioex_read(ioex, IT8801_REG_GPIO_CR(port, mask), &val);
	if (rv) {
		mutex_unlock(&ioex_mutex);
		return rv;
	}
	/* Select open drain 0:push-pull 1:open-drain */
	if (flags & GPIO_OPEN_DRAIN)
		val |= IT8801_GPIOIOT;
	else
		val &= ~IT8801_GPIOIOT;

	/* Select GPIO direction */
	if (flags & GPIO_OUTPUT) {
		/* Configure the output level */
		if (flags & GPIO_HIGH)
			it8801_gpio_sov[port] |= mask;
		else
			it8801_gpio_sov[port] &= ~mask;

		rv = it8801_ioex_write(ioex, IT8801_REG_GPIO_SOVR(port),
						it8801_gpio_sov[port]);
		if (rv) {
			mutex_unlock(&ioex_mutex);
			return rv;
		}
		val |= IT8801_GPIODIR;
	} else
		val &= ~IT8801_GPIODIR;

	rv = it8801_ioex_write(ioex, IT8801_REG_GPIO_CR(port, mask), val);
	mutex_unlock(&ioex_mutex);

	return rv;
}

const struct ioexpander_drv it8801_ioexpander_drv = {
	.init              = &it8801_ioex_init,
	.get_level         = &it8801_ioex_get_level,
	.set_level         = &it8801_ioex_set_level,
	.get_flags_by_mask = &it8801_ioex_get_flags_by_mask,
	.set_flags_by_mask = &it8801_ioex_set_flags_by_mask,
};

static void dump_register(int reg)
{
	int rv;
	int data;

	ccprintf("[%Xh] = ", reg);

	rv = it8801_read(reg, &data);

	if (!rv)
		ccprintf("0x%02x\n", data);
	else
		ccprintf("ERR (%d)\n", rv);
}

static int it8801_dump(int argc, char **argv)
{
	dump_register(IT8801_REG_KSIIER);
	dump_register(IT8801_REG_KSIEER);
	dump_register(IT8801_REG_KSIDR);
	dump_register(IT8801_REG_KSOMCR);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(it8801_dump, it8801_dump, "NULL",
			"Dumps IT8801 registers");

#ifdef CONFIG_IO_EXPANDER_IT8801_PWM

struct it8801_pwm_gpio_map {
	int port;
	int mask;
	int pushpull_en;
};

const static struct it8801_pwm_gpio_map it8801_pwm_gpio_map[] = {
	[1] = {.port = 1, .mask = BIT(2), .pushpull_en = BIT(0)},
	[2] = {.port = 1, .mask = BIT(3), .pushpull_en = BIT(1)},
	[3] = {.port = 1, .mask = BIT(4), .pushpull_en = BIT(2)},
	[4] = {.port = 1, .mask = BIT(5), .pushpull_en = BIT(3)},
	[7] = {.port = 2, .mask = BIT(0), .pushpull_en = BIT(4)},
	[8] = {.port = 2, .mask = BIT(3), .pushpull_en = BIT(5)},
	[9] = {.port = 2, .mask = BIT(2), .pushpull_en = BIT(6)},
};

void it8801_pwm_enable(enum pwm_channel ch, int enabled)
{
	int port, mask, val, index;

	index = it8801_pwm_channels[ch].index;
	if (index < 0 || index >= ARRAY_SIZE(it8801_pwm_gpio_map))
		return;
	port = it8801_pwm_gpio_map[index].port;
	mask = it8801_pwm_gpio_map[index].mask;
	if (port == 0 && mask == 0)
		return;

	/*
	 * PWM1~4,7: alt func 1
	 * PWM8,9: alt func 2
	 */
	if (it8801_pwm_channels[ch].index <= 7)
		it8801_write(IT8801_REG_GPIO_CR(port, mask),
				0x1 << IT8801_GPIOAFS_SHIFT);
	else
		it8801_write(IT8801_REG_GPIO_CR(port, mask),
				0x2 << IT8801_GPIOAFS_SHIFT);

	it8801_read(IT8801_REG_PWMMCR(it8801_pwm_channels[ch].index), &val);
	val &= (~IT8801_PWMMCR_MCR_MASK);
	if (enabled)
		val |= IT8801_PWMMCR_MCR_BLINKING;
	it8801_write(IT8801_REG_PWMMCR(it8801_pwm_channels[ch].index), val);

	/*
	 * 1: enable push pull function
	 */
	it8801_read(IT8801_REG_PWMODDSR, &val);
	val &= ~it8801_pwm_gpio_map[index].pushpull_en;
	if (enabled)
		val |= it8801_pwm_gpio_map[index].pushpull_en;
	it8801_write(IT8801_REG_PWMODDSR, val);

}

int it8801_pwm_get_enabled(enum pwm_channel ch)
{
	int val;

	if (it8801_read(IT8801_REG_PWMMCR(it8801_pwm_channels[ch].index), &val))
		return 0;
	return (val & IT8801_PWMMCR_MCR_MASK) == IT8801_PWMMCR_MCR_BLINKING;
}

void it8801_pwm_set_raw_duty(enum pwm_channel ch, uint16_t duty)
{
	duty = MIN(duty, 255);
	duty = MAX(duty, 0);
	it8801_write(IT8801_REG_PWMDCR(it8801_pwm_channels[ch].index), duty);
}

uint16_t it8801_pwm_get_raw_duty(enum pwm_channel ch)
{
	int val;

	if (it8801_read(IT8801_REG_PWMDCR(it8801_pwm_channels[ch].index), &val))
		return 0;
	return val;
}

void it8801_pwm_set_duty(enum pwm_channel ch, int percent)
{
	return it8801_pwm_set_raw_duty(ch, percent * 255 / 100);
}

int it8801_pwm_get_duty(enum pwm_channel ch)
{
	return it8801_pwm_get_raw_duty(ch) * 100 / 255;
}

#ifdef CONFIG_KEYBOARD_BACKLIGHT
const enum pwm_channel it8801_kblight_pwm_ch = IT8801_PWM_CH_KBLIGHT;

static int it8801_kblight_enable(int enable)
{
	it8801_pwm_enable(it8801_kblight_pwm_ch, enable);
	return EC_SUCCESS;
}

static int it8801_kblight_set_brightness(int percent)
{
	it8801_pwm_set_duty(it8801_kblight_pwm_ch, percent);
	return EC_SUCCESS;
}

static int it8801_kblight_get_brightness(void)
{
	return it8801_pwm_get_duty(it8801_kblight_pwm_ch);
}

static int it8801_kblight_init(void)
{
	it8801_pwm_set_duty(it8801_kblight_pwm_ch, 0);
	it8801_pwm_enable(it8801_kblight_pwm_ch, 1);
	return EC_SUCCESS;
}

const struct kblight_drv kblight_it8801 = {
	.init = it8801_kblight_init,
	.set = it8801_kblight_set_brightness,
	.get = it8801_kblight_get_brightness,
	.enable = it8801_kblight_enable,
};
#endif
#endif  /* CONFIG_IO_EXPANDER_IT8801_PWM */
