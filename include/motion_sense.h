/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header for motion_sense.c */

#ifndef __CROS_EC_MOTION_SENSE_H
#define __CROS_EC_MOTION_SENSE_H

#include "math_util.h"

/**
 * This structure defines all of the data needed to specify the orientation
 * of the base and lid accelerometers in order to calculate the lid angle.
 */
struct accel_orientation {
	/*
	 * Rotation matrix to rotate the lid sensor into the same reference
	 * frame as the base sensor.
	 */
	matrix_3x3_t rot_align;

	/* Rotation matrix to rotate positive 90 degrees around the hinge. */
	matrix_3x3_t rot_hinge_90;

	/*
	 * Rotation matrix to rotate 180 degrees around the hinge. The value
	 * here should be rot_hinge_90 ^ 2.
	 */
	matrix_3x3_t rot_hinge_180;

	/*
	 * Rotation matrix to rotate base sensor into the standard reference
	 * frame.
	 */
	matrix_3x3_t rot_standard_ref;

	/* Vector pointing along hinge axis. */
	vector_3_t hinge_axis;
};

/* Link global structure for orientation. This must be defined in board.c. */
extern
#ifndef CONFIG_ACCEL_CALIBRATE
const
#endif
struct accel_orientation acc_orient;


/**
 * Get last calculated lid angle.
 *
 * @return lid angle in degrees in range [-180, 180].
 */
int motion_get_lid_angle(void);


#ifdef CONFIG_ACCEL_CALIBRATE
/**
 * Get the last measured lid acceleration vector.
 *
 * @param v Pointer to location to store vector.
 * @param adjusted If false use the raw vector, if true use the adjusted vector.
 */
void motion_get_accel_lid(vector_3_t *v, int adjusted);

/**
 * Get the last measured base acceleration vector.
 *
 * @param v Pointer to location to store vector.
 */
void motion_get_accel_base(vector_3_t *v);
#endif


#endif /* __CROS_EC_MOTION_SENSE_H */
