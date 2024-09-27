/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Driver for tuning the MP2964 IMVP8 - IMVP9.1 parameters */

#include "console.h"
#include "i2c.h"
#include "mp2964.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

#define MP2964_STARTUP_WAIT_US (50 * MSEC)
#define MP2964_STORE_WAIT_US (300 * MSEC)
#define MP2964_RESTORE_WAIT_US (2 * MSEC)

enum reg_page { REG_PAGE_0, REG_PAGE_1, REG_PAGE_COUNT };

static int mp2964_write8(uint8_t reg, uint8_t value)
{
	const uint8_t tx[2] = { reg, value };

	return i2c_xfer_unlocked(I2C_PORT_MP2964, I2C_ADDR_MP2964_FLAGS, tx,
				 sizeof(tx), NULL, 0, I2C_XFER_SINGLE);
}

static void mp2964_read16(uint8_t reg, uint16_t *value)
{
	const uint8_t tx[1] = { reg };
	uint8_t rx[2];

	i2c_xfer_unlocked(I2C_PORT_MP2964, I2C_ADDR_MP2964_FLAGS, tx,
			  sizeof(tx), rx, sizeof(rx), I2C_XFER_SINGLE);
	*value = (rx[1] << 8) | rx[0];
}

static void mp2964_write16(uint8_t reg, uint16_t value)
{
	const uint8_t tx[3] = { reg, value & 0xff, value >> 8 };

	i2c_xfer_unlocked(I2C_PORT_MP2964, I2C_ADDR_MP2964_FLAGS, tx,
			  sizeof(tx), NULL, 0, I2C_XFER_SINGLE);
}

static int mp2964_select_page(enum reg_page page)
{
	int status;

	if (page >= REG_PAGE_COUNT)
		return EC_ERROR_INVAL;

	status = mp2964_write8(MP2964_PAGE, page);
	if (status != EC_SUCCESS) {
		CPRINTF("%s: could not select page 0x%02x, error %d\n",
			__func__, page, status);
	}
	return status;
}

static void mp2964_write_vec16(const struct mp2964_reg_val *init_list,
			       int count, int *delta)
{
	const struct mp2964_reg_val *reg_val;
	uint16_t outval;
	int i;

	reg_val = init_list;
	for (i = 0; i < count; ++i, ++reg_val) {
		mp2964_read16(reg_val->reg, &outval);
		if (outval == reg_val->val) {
#ifdef CONFIG_PLATFORM_EC_BRINGUP
			CPRINTF("mp2964: reg 0x%02x already 0x%04x\n",
				reg_val->reg, outval);
#endif /* CONFIG_PLATFORM_EC_BRINGUP */
			continue;
		}
#ifdef CONFIG_PLATFORM_EC_BRINGUP
		CPRINTF("mp2964: tuning reg 0x%02x from 0x%04x to 0x%04x\n",
			reg_val->reg, outval, reg_val->val);
#endif /* CONFIG_PLATFORM_EC_BRINGUP */
		mp2964_write16(reg_val->reg, reg_val->val);
		*delta += 1;
	}
}

static int mp2964_store_user_all(void)
{
	const uint8_t wr = MP2964_STORE_USER_ALL;
	const uint8_t rd = MP2964_RESTORE_USER_ALL;
	int status;

	CPRINTF("%s: updating persistent settings\n", __func__);

	status = i2c_xfer_unlocked(I2C_PORT_MP2964, I2C_ADDR_MP2964_FLAGS, &wr,
				   sizeof(wr), NULL, 0, I2C_XFER_SINGLE);
	if (status != EC_SUCCESS)
		return status;

	crec_usleep(MP2964_STORE_WAIT_US);

	status = i2c_xfer_unlocked(I2C_PORT_MP2964, I2C_ADDR_MP2964_FLAGS, &rd,
				   sizeof(rd), NULL, 0, I2C_XFER_SINGLE);
	if (status != EC_SUCCESS)
		return status;

	crec_usleep(MP2964_RESTORE_WAIT_US);

	return EC_SUCCESS;
}

static void mp2964_patch_rail(enum reg_page page,
			      const struct mp2964_reg_val *page_vals, int count,
			      int *delta)
{
	if (mp2964_select_page(page) != EC_SUCCESS)
		return;
	mp2964_write_vec16(page_vals, count, delta);
}

int mp2964_tune(const struct mp2964_reg_val *rail_a, int count_a,
		const struct mp2964_reg_val *rail_b, int count_b)
{
	int tries = 2;
	int delta;

	udelay(MP2964_STARTUP_WAIT_US);

	i2c_lock(I2C_PORT_MP2964, 1);

	do {
		int status;

		delta = 0;
		mp2964_patch_rail(REG_PAGE_0, rail_a, count_a, &delta);
		mp2964_patch_rail(REG_PAGE_1, rail_b, count_b, &delta);
		if (delta == 0)
			break;

		status = mp2964_store_user_all();
		if (status != EC_SUCCESS)
			CPRINTF("%s: STORE_USER_ALL failed\n", __func__);
	} while (--tries > 0);

	i2c_lock(I2C_PORT_MP2964, 0);

	if (delta)
		return EC_ERROR_UNKNOWN;
	else
		return EC_SUCCESS;
}
