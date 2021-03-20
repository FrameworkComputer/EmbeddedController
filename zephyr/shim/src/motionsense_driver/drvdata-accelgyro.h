/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Macros are to help creating driver data. A driver data that uses
 * any data structures defined in accelgyro.h should use the macros here
 * to utilize the information in device tree.
 *
 */
#ifndef __ZEPHYR_SHIM_SRC_MOTIONSENSE_DRIVER_DRVDATA_ACCELGYRO_H
#define __ZEPHYR_SHIM_SRC_MOTIONSENSE_DRIVER_DRVDATA_ACCELGYRO_H

/*
 * compatible = "cros-ec,accelgyro-als-channel-scale"
 * als_channel_scale_t in accelgyro.h
 *
 * e.g) The following is the example in DT for als_channel_scale_t
 * als-channel-scale {
 *	compatible = "cros-ec,accelgyro-als-channel-scale";
 *		k-channel-scale = <1>;
 *		cover-scale = <1>;
 *	};
 */
#define ACCELGYRO_ALS_CHANNEL_SCALE(id)					\
	{								\
		.k_channel_scale =					\
			ALS_CHANNEL_SCALE(DT_PROP(id, k_channel_scale)),\
		.cover_scale =						\
			ALS_CHANNEL_SCALE(DT_PROP(id, cover_scale)),	\
	}

#define ALS_CALIBRATION_CHANNEL_SCALE(id)				\
	.als_cal.channel_scale = ACCELGYRO_ALS_CHANNEL_SCALE(id),

#define ALS_CALIBRATION_SET(id)					\
	.als_cal.scale = DT_PROP(id, scale),			\
	.als_cal.uscale = DT_PROP(id, uscale),			\
	.als_cal.offset = DT_PROP(id, offset),			\
	ALS_CALIBRATION_CHANNEL_SCALE(DT_CHILD(id, als_channel_scale))

/*
 * compatible = "cros-ec,accelgyro-als-drv-data"
 * als_drv_data_t in accelgyro.h
 *
 * e.g) The following is the example in DT for als_drv_data_t 
 * als-drv-data {
 *	compatible = "cros-ec,accelgyro-als-drv-data";
 *	als-cal {
 *		scale = <1>;
 *		uscale = <0>;
 *		offset = <0>;
 *		als-channel-scale {
 *		compatible = "cros-ec,accelgyro-als-channel-scale";
 *			k-channel-scale = <1>;
 *			cover-scale = <1>;
 *		};
 *	};
 * };
 */
#define ACCELGYRO_ALS_DRV_DATA(id)					\
	{								\
		ALS_CALIBRATION_SET(DT_CHILD(id, als_cal))		\
	}

#define RGB_CAL_RGB_SET_SCALE(id)			\
	.scale = ACCELGYRO_ALS_CHANNEL_SCALE(id),

#define RGB_CAL_RGB_SET_ONE(id, suffix)					\
	.rgb_cal[suffix] = {						\
	    .offset = DT_PROP(id, offset),				\
	    .coeff[0] = FLOAT_TO_FP(DT_PROP_BY_IDX(id, coeff, 0)),	\
	    .coeff[1] = FLOAT_TO_FP(DT_PROP_BY_IDX(id, coeff, 1)),	\
	    .coeff[2] = FLOAT_TO_FP(DT_PROP_BY_IDX(id, coeff, 2)),	\
	    .coeff[3] = FLOAT_TO_FP(DT_PROP_BY_IDX(id, coeff, 3)),	\
	    RGB_CAL_RGB_SET_SCALE(DT_CHILD(id, als_channel_scale))	\
	},

/*
 * compatible = "cros-ec,accelgyro-rgb-calibration"
 * rgb_calibration_t in accelgyro.h
 *
 * e.g) The following is the example in DT for rgb_calibration_t
 * rgb_calibration {
 * 	compatible = "cros-ec,accelgyro-rgb-calibration";
 *
 *	irt = <1>;
 *
 *	rgb-cal-x {
 *		offset = <0>;
 *		coeff = <0 0 0 0>;
 *		als-channel-scale {
 *		compatible = "cros-ec,accelgyro-als-channel-scale";
 *			k-channel-scale = <1>;
 *			cover-scale = <1>;
 *		};
 *	};
 *	rgb-cal-y {
 *		offset = <0>;
 *		coeff = <0 0 0 0>;
 *		als-channel-scale {
 *		compatible = "cros-ec,accelgyro-als-channel-scale";
 *			k-channel-scale = <1>;
 *			cover-scale = <1>;
 *		};
 *	};
 *	rgb-cal-z {
 *		offset = <0>;
 *		coeff = <0 0 0 0>;
 *		als-channel-scale {
 *		compatible = "cros-ec,accelgyro-als-channel-scale";
 *			k-channel-scale = <1>;
 *			cover-scale = <1>;
 *		};
 *	};
 * };
 */
#define ACCELGYRO_RGB_CALIBRATION(id)				\
	{							\
		RGB_CAL_RGB_SET_ONE(DT_CHILD(id, rgb_cal_x), X)	\
		RGB_CAL_RGB_SET_ONE(DT_CHILD(id, rgb_cal_y), Y)	\
		RGB_CAL_RGB_SET_ONE(DT_CHILD(id, rgb_cal_z), Z)	\
		.irt = INT_TO_FP(DT_PROP(id, irt)),		\
	}

#endif /* __ZEPHYR_SHIM_SRC_MOTIONSENSE_DRIVER_DRVDATA_ACCELGYRO_H */
