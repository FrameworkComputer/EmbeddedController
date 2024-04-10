/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Sensor Hub Driver for LSM6DSM acce/gyro module to enable connecting
 * external sensors like magnetometer
 */

#include "console.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/sensorhub_lsm6dsm.h"
#include "driver/stm_mems_common.h"

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)

static int set_reg_bit_field(const struct motion_sensor_t *s, uint8_t reg,
			     uint8_t bit_field)
{
	int tmp;
	int ret;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, reg, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	tmp |= bit_field;
	return st_raw_write8(s->port, s->i2c_spi_addr_flags, reg, tmp);
}

static int clear_reg_bit_field(const struct motion_sensor_t *s, uint8_t reg,
			       uint8_t bit_field)
{
	int tmp;
	int ret;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, reg, &tmp);
	if (ret != EC_SUCCESS)
		return ret;

	tmp &= ~(bit_field);
	return st_raw_write8(s->port, s->i2c_spi_addr_flags, reg, tmp);
}

static inline int enable_sensorhub_func(const struct motion_sensor_t *s)
{
	return set_reg_bit_field(s, LSM6DSM_CTRL10_ADDR, LSM6DSM_EMBED_FUNC_EN);
}

static inline int disable_sensorhub_func(const struct motion_sensor_t *s)
{
	return clear_reg_bit_field(s, LSM6DSM_CTRL10_ADDR,
				   LSM6DSM_EMBED_FUNC_EN);
}

/*
 * Sensor hub includes embedded register banks associated with external
 * sensors. 4 external sensor slaves can be attached to the sensor hub
 * and hence 4 such register banks exist. The access to them are disabled
 * by default. Below 2 helper functions help enable/disable access to those
 * register banks.
 */
static inline int enable_ereg_bank_acc(const struct motion_sensor_t *s)
{
	return set_reg_bit_field(s, LSM6DSM_FUNC_CFG_ACC_ADDR,
				 LSM6DSM_FUNC_CFG_EN);
}

static inline int disable_ereg_bank_acc(const struct motion_sensor_t *s)
{
	return clear_reg_bit_field(s, LSM6DSM_FUNC_CFG_ACC_ADDR,
				   LSM6DSM_FUNC_CFG_EN);
}

static inline int enable_aux_i2c_controller(const struct motion_sensor_t *s)
{
	return set_reg_bit_field(s, LSM6DSM_CONTROLLER_CFG_ADDR,
				 LSM6DSM_I2C_CONTROLLER_ON);
}

static inline int disable_aux_i2c_controller(const struct motion_sensor_t *s)
{
	return clear_reg_bit_field(s, LSM6DSM_CONTROLLER_CFG_ADDR,
				   LSM6DSM_I2C_CONTROLLER_ON);
}

static inline int restore_controller_cfg(const struct motion_sensor_t *s,
					 int cache)
{
	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     LSM6DSM_CONTROLLER_CFG_ADDR, cache);
}

static int enable_i2c_pass_through(const struct motion_sensor_t *s, int *cache)
{
	int ret;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags,
			   LSM6DSM_CONTROLLER_CFG_ADDR, cache);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x MCR error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	/*
	 * Fake set sensor hub to external trigger event and wait for 10ms.
	 * Wait is for any pending bus activity(probably read) to settle down
	 * so that there is no bus contention.
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSM_CONTROLLER_CFG_ADDR,
			    *cache | LSM6DSM_EXT_TRIGGER_EN);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x MCETEN error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}
	crec_msleep(10);

	ret = st_raw_write8(
		s->port, s->i2c_spi_addr_flags, LSM6DSM_CONTROLLER_CFG_ADDR,
		*cache & ~(LSM6DSM_EXT_TRIGGER_EN | LSM6DSM_I2C_CONTROLLER_ON));
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x MCC error ret: %d\n", __func__,
			s->name, s->type, ret);
		restore_controller_cfg(s, *cache);
		return ret;
	}

	return st_raw_write8(s->port, s->i2c_spi_addr_flags,
			     LSM6DSM_CONTROLLER_CFG_ADDR,
			     LSM6DSM_I2C_PASS_THRU_MODE);
}

static inline int power_down_accel(const struct motion_sensor_t *s, int *cache)
{
	int ret;

	ret = st_raw_read8(s->port, s->i2c_spi_addr_flags, LSM6DSM_CTRL1_ADDR,
			   cache);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x CTRL1R error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	return st_raw_write8(s->port, s->i2c_spi_addr_flags, LSM6DSM_CTRL1_ADDR,
			     *cache & ~LSM6DSM_XL_ODR_MASK);
}

static inline int restore_ctrl1(const struct motion_sensor_t *s, int cache)
{
	return st_raw_write8(s->port, s->i2c_spi_addr_flags, LSM6DSM_CTRL1_ADDR,
			     cache);
}

static int config_slv0_read(const struct motion_sensor_t *s,
			    const uint16_t slv_addr_flags, uint16_t reg,
			    uint8_t len)
{
	int ret;
	uint16_t addr_8bit = I2C_STRIP_FLAGS(slv_addr_flags) << 1;

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSM_SLV0_ADD_ADDR,
			    (addr_8bit | LSM6DSM_SLV0_RD_BIT));
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x SA error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSM_SLV0_SUBADD_ADDR, reg);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x RA error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	/*
	 * No decimation for external sensor 0,
	 * Number of sensors connected to external sensor hub 1
	 */
	ret = st_raw_write8(s->port, s->i2c_spi_addr_flags,
			    LSM6DSM_SLV0_CONFIG_ADDR,
			    (len & LSM6DSM_SLV0_NUM_OPS_MASK));
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x CFG error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	return EC_SUCCESS;
}

int sensorhub_config_ext_reg(const struct motion_sensor_t *s,
			     const uint16_t slv_addr_flags, uint8_t reg,
			     uint8_t val)
{
	int ret;
	int tmp;

	ret = enable_i2c_pass_through(s, &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x ENI2C error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	ret = st_raw_write8(s->port, slv_addr_flags, reg, val);
	restore_controller_cfg(s, tmp);
	return ret;
}

int sensorhub_config_slv0_read(const struct motion_sensor_t *s,
			       uint16_t slv_addr_flags, uint8_t reg, int len)
{
	int tmp_xl_cfg;
	int ret;

	if (len <= 0 || len > OUT_XYZ_SIZE) {
		CPRINTF("%s: %s type:0x%x Invalid length: %d\n", __func__,
			s->name, s->type, len);
		return EC_ERROR_INVAL;
	}

	ret = power_down_accel(s, &tmp_xl_cfg);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x PDXL error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	ret = enable_ereg_bank_acc(s);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x ENERB error ret: %d\n", __func__,
			s->name, s->type, ret);
		goto out_restore_ctrl1;
	}

	ret = config_slv0_read(s, slv_addr_flags, reg, len);
	disable_ereg_bank_acc(s);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x CS0R error ret: %d\n", __func__,
			s->name, s->type, ret);
		goto out_restore_ctrl1;
	}

	ret = enable_sensorhub_func(s);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x ENSH error ret: %d\n", __func__,
			s->name, s->type, ret);
		goto out_restore_ctrl1;
	}

	ret = enable_aux_i2c_controller(s);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x ENI2CM error ret: %d\n", __func__,
			s->name, s->type, ret);
		disable_sensorhub_func(s);
	}
out_restore_ctrl1:
	restore_ctrl1(s, tmp_xl_cfg);
	return ret;
}

int sensorhub_slv0_data_read(const struct motion_sensor_t *s, uint8_t *raw)
{
	int ret;

	/*
	 * Accel/Gyro is already reading slave 0 data into the sensorhub1
	 * register as soon as the accel is in power-up mode. So return the
	 * contents of that register.
	 */
	ret = st_raw_read_n_noinc(s->port, s->i2c_spi_addr_flags,
				  LSM6DSM_SENSORHUB1_REG, raw, OUT_XYZ_SIZE);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x SH1R error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}
	return EC_SUCCESS;
}

int sensorhub_check_and_rst(const struct motion_sensor_t *s,
			    const uint16_t slv_addr_flags, uint8_t whoami_reg,
			    uint8_t whoami_val, uint8_t rst_reg,
			    uint8_t rst_val)
{
	int ret, tmp;
	int tmp_controller_cfg;

	ret = enable_i2c_pass_through(s, &tmp_controller_cfg);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x ENI2C error ret: %d\n", __func__,
			s->name, s->type, ret);
		return ret;
	}

	ret = st_raw_read8(s->port, slv_addr_flags, whoami_reg, &tmp);
	if (ret != EC_SUCCESS) {
		CPRINTF("%s: %s type:0x%x WAIR error ret: %d\n", __func__,
			s->name, s->type, ret);
		goto err_restore_controller_cfg;
	}

	if (tmp != whoami_val) {
		CPRINTF("%s: %s type:0x%x WAIC error ret: %d\n", __func__,
			s->name, s->type, ret);
		ret = EC_ERROR_UNKNOWN;
		goto err_restore_controller_cfg;
	}

	ret = st_raw_write8(s->port, slv_addr_flags, rst_reg, rst_val);
err_restore_controller_cfg:
	restore_controller_cfg(s, tmp_controller_cfg);
	return ret;
}
