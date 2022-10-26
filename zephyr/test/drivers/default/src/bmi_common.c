/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "accelgyro_bmi_common.h"
#include "i2c.h"
#include "emul/emul_bmi.h"
#include "emul/emul_common_i2c.h"
#include "test/drivers/test_state.h"

#define BMI_NODE DT_NODELABEL(accel_bmi160)
#define BMI_ACC_SENSOR_ID SENSOR_ID(DT_NODELABEL(ms_bmi160_accel))

FAKE_VALUE_FUNC(int, i2c_write_handler, const struct emul *, int, uint8_t, int,
		void *);
FAKE_VALUE_FUNC(int, i2c_read_handler, const struct emul *, int, uint8_t *, int,
		void *);

void bmi_common_before(void *f)
{
	ARG_UNUSED(f);

	RESET_FAKE(i2c_write_handler);
	RESET_FAKE(i2c_read_handler);
}

void bmi_common_after(void *f)
{
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	ARG_UNUSED(f);

	motion_sensors[BMI_ACC_SENSOR_ID].type = MOTIONSENSE_TYPE_ACCEL;
	i2c_common_emul_set_write_func(common_data, NULL, NULL);
	i2c_common_emul_set_read_func(common_data, NULL, NULL);
}

ZTEST_SUITE(bmi_common, drivers_predicate_post_main, NULL, bmi_common_before,
	    bmi_common_after, NULL);

ZTEST(bmi_common, test_get_xyz_reg_mag)
{
	int reg;

	motion_sensors[BMI_ACC_SENSOR_ID].type = MOTIONSENSE_TYPE_MAG;
	reg = bmi_get_xyz_reg(&motion_sensors[BMI_ACC_SENSOR_ID]);

	zassert_equal(BMI160_MAG_X_L_G, reg, "Expected %d, but got %d",
		      BMI160_MAG_X_L_G, reg);

	motion_sensors[BMI_ACC_SENSOR_ID].type = MOTIONSENSE_TYPE_PROX;
	reg = bmi_get_xyz_reg(&motion_sensors[BMI_ACC_SENSOR_ID]);

	zassert_equal(-1, reg, "Expected 0, but got %d", reg);
}

ZTEST(bmi_common, test_write16)
{
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);

	i2c_write_handler_fake.return_val = 0;
	i2c_common_emul_set_write_func(common_data, i2c_write_handler, NULL);
	zassert_ok(bmi_write16(
		motion_sensors[BMI_ACC_SENSOR_ID].port,
		motion_sensors[BMI_ACC_SENSOR_ID].i2c_spi_addr_flags, 0,
		0x1234));
	zassert_equal(2, i2c_write_handler_fake.call_count);
	zassert_equal(0, i2c_write_handler_fake.arg1_history[0]);
	zassert_equal(0, i2c_write_handler_fake.arg1_history[1]);
	zassert_equal(0x34, i2c_write_handler_fake.arg2_history[0],
		      "got 0x%02x", i2c_write_handler_fake.arg2_history[0]);
	zassert_equal(0x12, i2c_write_handler_fake.arg2_history[1],
		      "got 0x%02x", i2c_write_handler_fake.arg2_history[1]);
	zassert_equal(1, i2c_write_handler_fake.arg3_history[0], "got %d",
		      i2c_write_handler_fake.arg3_history[0]);
	zassert_equal(2, i2c_write_handler_fake.arg3_history[1], "got %d",
		      i2c_write_handler_fake.arg3_history[1]);
}

ZTEST(bmi_common, test_read32)
{
	const struct emul *emul = EMUL_DT_GET(BMI_NODE);
	struct i2c_common_emul_data *common_data =
		emul_bmi_get_i2c_common_data(emul);
	int data = 0;

	i2c_read_handler_fake.return_val = 0;
	i2c_common_emul_set_read_func(common_data, i2c_read_handler, NULL);
	zassert_ok(
		bmi_read32(motion_sensors[BMI_ACC_SENSOR_ID].port,
			   motion_sensors[BMI_ACC_SENSOR_ID].i2c_spi_addr_flags,
			   0, &data));
	zassert_equal(4, i2c_read_handler_fake.call_count);
	zassert_equal(0, i2c_read_handler_fake.arg1_history[0]);
	zassert_equal(0, i2c_read_handler_fake.arg1_history[1]);
	zassert_equal(0, i2c_read_handler_fake.arg1_history[2]);
	zassert_equal(0, i2c_read_handler_fake.arg1_history[3]);
	zassert_equal(0, i2c_read_handler_fake.arg3_history[0], "got %d",
		      i2c_read_handler_fake.arg3_history[0]);
	zassert_equal(1, i2c_read_handler_fake.arg3_history[1], "got %d",
		      i2c_read_handler_fake.arg3_history[1]);
	zassert_equal(2, i2c_read_handler_fake.arg3_history[2], "got %d",
		      i2c_read_handler_fake.arg3_history[2]);
	zassert_equal(3, i2c_read_handler_fake.arg3_history[3], "got %d",
		      i2c_read_handler_fake.arg3_history[3]);
}

ZTEST(bmi_common, test_list_activities)
{
	struct bmi_drv_data_t *data =
		BMI_GET_DATA(&motion_sensors[BMI_ACC_SENSOR_ID]);
	uint32_t enabled = 0;
	uint32_t disabled = 0;

	data->enabled_activities = 0x12;
	data->disabled_activities = 0x34;

	zassert_ok(bmi_list_activities(&motion_sensors[BMI_ACC_SENSOR_ID],
				       &enabled, &disabled));
	zassert_equal(0x12, enabled);
	zassert_equal(0x34, disabled);
}
