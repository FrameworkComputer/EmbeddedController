/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "accelgyro.h"

#define SENSOR_MUTEX_NODE		DT_PATH(motionsense_mutex)
#define SENSOR_MUTEX_NAME(id)		DT_CAT(MUTEX_, id)

#if DT_NODE_EXISTS(SENSOR_MUTEX_NODE)
#define DECLARE_SENSOR_MUTEX(id)	static mutex_t SENSOR_MUTEX_NAME(id);
#define INIT_SENSOR_MUTEX(id)		k_mutex_init(&SENSOR_MUTEX_NAME(id));

/*
 * Declare mutex for
 * each child node of "/motionsense-mutex" node in DT.
 *
 * A mutex can be shared among the motion sensors.
 */
DT_FOREACH_CHILD(SENSOR_MUTEX_NODE, DECLARE_SENSOR_MUTEX)

/* Initialize mutexes */
static int init_sensor_mutex(const struct device *dev)
{
	ARG_UNUSED(dev);

	DT_FOREACH_CHILD(SENSOR_MUTEX_NODE, INIT_SENSOR_MUTEX)

	return 0;
}
SYS_INIT(init_sensor_mutex, POST_KERNEL, 50);
#endif /* DT_NODE_EXISTS(SENSOR_MUTEX_NODE) */

#define SENSOR_ROT_REF_NODE		DT_PATH(motionsense_rotation_ref)
#define SENSOR_ROT_STD_REF_NAME(id)	DT_CAT(ROT_REF_, id)
#define MAT_ITEM(i, id)	FLOAT_TO_FP((int32_t)(DT_PROP_BY_IDX(id, mat33, i)))
#define DECLARE_SENSOR_ROT_REF(id)					\
	static const mat33_fp_t SENSOR_ROT_STD_REF_NAME(id) = {	\
		{							\
			FOR_EACH_FIXED_ARG(MAT_ITEM, (,), id, 0, 1, 2)	\
		},							\
		{							\
			FOR_EACH_FIXED_ARG(MAT_ITEM, (,), id, 3, 4, 5)	\
		},							\
		{							\
			FOR_EACH_FIXED_ARG(MAT_ITEM, (,), id, 6, 7, 8)	\
		},							\
	};

/*
 * Declare 3x3 rotation matrix for
 * each child node of "/motionsense-rotation-ref" node in DT.
 *
 * A rotation matrix can be shared among the motion sensors.
 */
#if DT_NODE_EXISTS(SENSOR_ROT_REF_NODE)
DT_FOREACH_CHILD(SENSOR_ROT_REF_NODE, DECLARE_SENSOR_ROT_REF)
#endif

/*
 * Declare sensor driver data for
 * each child node with status = "okay" of
 * "/motionsense-sensor-data" node in DT.
 *
 * A driver data can be shared among the motion sensors.
 */
#define SENSOR_DATA_NAME(id)		DT_CAT(SENSOR_DAT_, id)
#define SENSOR_DATA_NODE		DT_PATH(motionsense_sensor_data)

#define SENSOR_DATA(inst, compat, create_data_macro)			\
	create_data_macro(DT_INST(inst, compat),			\
		SENSOR_DATA_NAME(DT_INST(inst, compat)))

/*
 * CREATE_SENSOR_DATA is a helper macro that gets
 * compat and create_data_macro as parameters.
 *
 * For each node with compatible = "compat",
 * CREATE_SENSOR_DATA expands "create_data_macro" macro with the node id and
 * the designated name for the sensor driver data to be created. The
 * "create_datda_macro" macro is responsible for creating the sensor driver
 * data with the name.
 *
 * Sensor drivers should provide <chip>-drvinfo.inc file and, in the file,
 * it should have the macro that creates its sensor driver data using device
 * tree and pass the macro via CREATE_SENSOR_DATA.
 *
 * e.g) The below is contents of tcs3400-drvinfo.inc file. The file has
 * CREATE_SENSOR_DATA_TCS3400_CLEAR that creates the static instance of
 * "struct als_drv_data_t" with the given name and initializes it
 * with device tree. Then use CREATE_SENSOR_DATA.
 *
 * ----------- bma255-drvinfo.inc -----------
 * #define CREATE_SENSOR_DATA_TCS3400_CLEAR(id, drvdata_name)      \
 *       static struct als_drv_data_t drvdata_name =               \
 *           ACCELGYRO_ALS_DRV_DATA(DT_CHILD(id, als_drv_data));
 *
 * CREATE_SENSOR_DATA(cros_ec_drvdata_tcs3400_clear,   \
 *                    CREATE_SENSOR_DATA_TCS3400_CLEAR)
 */
#define CREATE_SENSOR_DATA(compat, create_data_macro)			\
	UTIL_LISTIFY(DT_NUM_INST_STATUS_OKAY(compat), SENSOR_DATA,	\
		compat, create_data_macro)

/*
 * Here, we declare all sensor driver data. How to create the data is
 * defined in <chip>-drvinfo.inc file and ,in turn, the file is included
 * in sensor_drv_list.inc.
 */
#if DT_NODE_EXISTS(SENSOR_DATA_NODE)
#include "motionsense_driver/sensor_drv_list.inc"
#endif
