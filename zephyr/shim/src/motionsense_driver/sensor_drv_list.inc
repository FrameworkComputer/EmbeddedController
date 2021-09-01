/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * A driver should create <chip>-drvinfo.inc to create
 * driver-specific data and an motion sensor entry in
 * motion_sensors array that are used by motion sense task.
 *
 * This file includes the .inc file and is used by motionsense_sensrs.c to
 * create the sensor driver data and the entries in mostion_sensors array.
 *
 * e.g) bma255-drvinfo.inc is provided for BMA255 chip
 *
 * #ifdef CONFIG_ACCEL_BMA255
 * #include "bma255-drvinfo.inc"
 * #endif
 */

/* supported sensor driver list */
#ifdef CONFIG_PLATFORM_EC_ACCEL_BMA255
#include "bma255-drvinfo.inc"
#endif
#ifdef CONFIG_PLATFORM_EC_ACCEL_KX022
#include "kx022-drvinfo.inc"
#endif
#ifdef CONFIG_PLATFORM_EC_ACCEL_LIS2DW12
#include "lis2dw12-drvinfo.inc"
#endif
#ifdef CONFIG_PLATFORM_EC_ACCELGYRO_BMI160
#include "bmi160-drvinfo.inc"
#endif
#ifdef CONFIG_PLATFORM_EC_ACCELGYRO_BMI260
#include "bmi260-drvinfo.inc"
#endif
#ifdef CONFIG_PLATFORM_EC_ALS_TCS3400
#include "tcs3400-drvinfo.inc"
#endif
#ifdef CONFIG_PLATFORM_EC_ACCELGYRO_ICM426XX
#include "icm426xx-drvinfo.inc"
#endif
