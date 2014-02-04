/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Motion sensor calibration code. */

#include "accelerometer.h"
#include "common.h"
#include "console.h"
#include "math_util.h"
#include "motion_sense.h"
#include "timer.h"
#include "task.h"
#include "util.h"

/*
 * Threshold to capture a sample when performing auto-calibrate. The units are
 * the same as the units of the accelerometer acceleration values.
 */
#define AUTO_CAL_DIR_THRESHOLD (ACCEL_G * 3 / 4)
#define AUTO_CAL_MAG_THRESHOLD (ACCEL_G / 20)

/*****************************************************************************/
/* Console commands */

/**
 * Print all orientation calibration data.
 */
static int command_print_orientation(int argc, char **argv)
{
	matrix_3x3_t (*R);

	R = &acc_orient.rot_align;
	ccprintf("Lid to base alignment R:\n%.2d\t%.2d\t%.2d\n%.2d\t%.2d\t%.2d"
			"\n%.2d\t%.2d\t%.2d\n\n",
	(int)((*R)[0][0]*100), (int)((*R)[0][1]*100), (int)((*R)[0][2]*100),
	(int)((*R)[1][0]*100), (int)((*R)[1][1]*100), (int)((*R)[1][2]*100),
	(int)((*R)[2][0]*100), (int)((*R)[2][1]*100), (int)((*R)[2][2]*100));

	R = &acc_orient.rot_hinge_90;
	ccprintf("Hinge rotation 90 R:\n%.2d\t%.2d\t%.2d\n%.2d\t%.2d\t%.2d\n"
			"%.2d\t%.2d\t%.2d\n\n",
	(int)((*R)[0][0]*100), (int)((*R)[0][1]*100), (int)((*R)[0][2]*100),
	(int)((*R)[1][0]*100), (int)((*R)[1][1]*100), (int)((*R)[1][2]*100),
	(int)((*R)[2][0]*100), (int)((*R)[2][1]*100), (int)((*R)[2][2]*100));

	R = &acc_orient.rot_hinge_180;
	ccprintf("Hinge rotation 180 R:\n%.2d\t%.2d\t%.2d\n%.2d\t%.2d\t%.2d\n"
			"%.2d\t%.2d\t%.2d\n\n",
	(int)((*R)[0][0]*100), (int)((*R)[0][1]*100), (int)((*R)[0][2]*100),
	(int)((*R)[1][0]*100), (int)((*R)[1][1]*100), (int)((*R)[1][2]*100),
	(int)((*R)[2][0]*100), (int)((*R)[2][1]*100), (int)((*R)[2][2]*100));

	ccprintf("Hinge Axis:\t%d\t%d\t%d\n", acc_orient.hinge_axis[0],
			acc_orient.hinge_axis[1],
			acc_orient.hinge_axis[2]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelorient, command_print_orientation,
	"",
	"Print all orientation calibration data", NULL);

/**
 * Calibrate the orientation and print results to console.
 *
 * @param type	0 is for calibrating lid to base alignment,
 *		1 is for calibrating hinge 90 rotation
 */
static int calibrate_orientation(int type)
{
	int mag, ret, i, j;

	/* Captured flags. Set true when the corresponding axis is captured. */
	int captured[3] = {0, 0, 0};

	/* Current acceleration vectors. */
	vector_3_t base, lid;

	static matrix_3x3_t rec_base, rec_lid;

	while (1) {
		/* Capture the current acceleration vectors. */
		motion_get_accel_lid(&lid, type);
		motion_get_accel_base(&base);

		/* Measure magnitude of base accelerometer. */
		mag = vector_magnitude(base);

		/*
		 * Only capture a sample if the magnitude of the acceleration
		 * is close to G, because this assures we won't calibrate with
		 * values biased by motion.
		 */
		if ((mag > ACCEL_G - AUTO_CAL_MAG_THRESHOLD) &&
			(mag < ACCEL_G + AUTO_CAL_MAG_THRESHOLD)) {

			/*
			 * Capture a sample when each axis exceeds some
			 * threshold. This guarantees linear independence.
			 */
			for (i = 0; i < 3; i++) {
				if (!captured[i] &&
					ABS(base[i]) > AUTO_CAL_DIR_THRESHOLD) {

					for (j = 0; j < 3; j++) {
						rec_base[i][j] = (float)base[j];
						rec_lid[i][j] = (float)lid[j];
					}
					ccprintf("Captured axis %d\n", i);
					captured[i] = 1;
				}
			}

			/* If all axes are captured, we are done. */
			if (captured[0] && captured[1] && captured[2])
				break;
		}

		/* Wait until next reading. */
		task_wait_event(50*MSEC);
	}

	/* Solve for the rotation matrix and display final rotation matrix. */
	if (type == 0)
		ret = solve_rotation_matrix(&rec_lid, &rec_base,
					&acc_orient.rot_align);
	else
		ret = solve_rotation_matrix(&rec_base, &rec_lid,
					&acc_orient.rot_hinge_90);

	if (ret != EC_SUCCESS)
		ccprintf("Failed to find rotation matrix.\n");

	return ret;
}

/**
 * Calibrate the hinge axis and hinge 180 rotation matrix.
 */
static int calibrate_hinge(void)
{
	static matrix_3x3_t tmp;
	float d;
	int i, j;
	vector_3_t base;

	motion_get_accel_base(&base);
	memcpy(&acc_orient.hinge_axis, &base, sizeof(vector_3_t));

	/*
	 * Calculate a rotation matrix to rotate 180 degrees about hinge axis.
	 * The formula is:
	 *
	 * rot_hinge_180 = I + 2 * tmp^2 / d^2,
	 * where tmp is a matrix formed from the hinge axis, d is the sqrt
	 * of the hinge axis vector used in tmp, and I is the 3x3 identity
	 * matrix.
	 *
	 */
	tmp[0][0] = 0;
	tmp[0][1] = base[2];
	tmp[0][2] = -base[1];
	tmp[1][0] = -base[2];
	tmp[1][1] = 0;
	tmp[1][2] = base[0];
	tmp[2][0] = base[1];
	tmp[2][1] = -base[0];
	tmp[2][2] = 0;

	matrix_multiply(&tmp, &tmp, &acc_orient.rot_hinge_180);
	d = (float)(SQ(base[0]) + SQ(base[1]) + SQ(base[2]));

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			acc_orient.rot_hinge_180[i][j] *= 2.0F / d;

			/* Add identity matrix. */
			if (i == j)
				acc_orient.rot_hinge_180[i][j] += 1;
		}
	}

	return EC_SUCCESS;
}

static int command_auto_calibrate(int argc, char **argv)
{
	char *e;
	int type, ret;
	static int last_type = -1;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	type = strtoi(argv[1], &e, 0);

	if (*e)
		return EC_ERROR_PARAM1;

	/*
	 * First time this issued, just display instructions and return. If
	 * command is repeated, then perform calibration.
	 */
	if (type != last_type) {
		/*
		 * type 0: calibrate the lid to base alignment rotation matrix.
		 * type 1: calibrate the hinge 90 rotation matrix.
		 * type 2: calibrate hinge axis and hinge 180 rotation matrix.
		 */
		switch (type) {
		case 0:
			ccprintf("To calibrate, close lid, issue this command "
				"again, and rotate the machine in space until "
				"all 3 directions are captured.\n");
			break;
		case 1:
			ccprintf("To calibrate, open lid to 90 degrees, issue "
				" this command again, and rotate in space "
				"until all 3 directions are captured.\n");
			break;
		case 2:
			ccprintf("To calibrate, align hinge with gravity, and "
				"issue this command again.\n");
			break;
		default:
			return EC_ERROR_PARAM1;
		}

		last_type = type;
		return EC_SUCCESS;
	}

	/* Call appropriate calibration function. */
	if (type == 0 || type == 1)
		ret = calibrate_orientation(type);
	else
		ret = calibrate_hinge();

	/* Print results of all calibration. */
	if (ret == EC_SUCCESS)
		command_print_orientation(0, NULL);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelcalib, command_auto_calibrate,
	"0 - Calibrate lid to base alignment rotation matrix\n1 - Calibrate "
	"hinge positive 90 rotation matrix\n2 - Calibrate hinge axis and hinge "
	"180 matrix",
	"Auto calibrate the accelerometers", NULL);
