/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

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
