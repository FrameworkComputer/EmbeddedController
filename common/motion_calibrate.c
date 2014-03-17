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
#include "uart.h"
#include "util.h"

/*
 * Threshold to capture a sample when performing auto-calibrate. The units are
 * the same as the units of the accelerometer acceleration values.
 */
#define AUTO_CAL_DIR_THRESHOLD (ACCEL_G * 3 / 4)
#define AUTO_CAL_MAG_THRESHOLD (ACCEL_G / 20)

/*
 * Solution to standard reference frame calibration equation. Note, this matrix
 * depends on the exact instructions regarding the orientation given to the user
 * for calibrating the standard reference frame.
 */
static matrix_3x3_t standard_ref_calib = {
	{ 1024,  0,  0},
	{ 0, -1024,  0},
	{ 0,  0,  1024}
};

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

	R = &acc_orient.rot_standard_ref;
	ccprintf("Standard ref frame R:\n%.2d\t%.2d\t%.2d\n%.2d\t%.2d\t%.2d\n"
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
		task_wait_event(50 * MSEC);
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

/**
 * Calibrate the standard reference frame.
 */
static int calibrate_standard_frame(vector_3_t *v_x, vector_3_t *v_y,
		vector_3_t *v_z)
{
	static matrix_3x3_t m;
	int j;

	for (j = 0; j < 3; j++) {
		m[0][j] = (*v_x)[j];
		m[1][j] = (*v_y)[j];
		m[2][j] = (*v_z)[j];
	}

	return solve_rotation_matrix(&m, &standard_ref_calib,
						&acc_orient.rot_standard_ref);
}

/**
 * Wait until a specific set of keys is pressed: enter, 'q', or 's'. Return
 * key that was pressed.
 */
static int wait_for_key(void)
{
	int c = uart_getc();

	/* Loop until previous character was a new line char, 'q', or 's'. */
	while (c != '\r' && c != '\n' && c != 'q' && c != 's') {
		task_wait_event(50 * MSEC);
		c = uart_getc();
	}

	return c;
}

static int command_auto_calibrate(int argc, char **argv)
{
	int c;
	vector_3_t v_x, v_y, v_z;

	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	ccprintf("Calibrating... press 'q' at any time to quit, and 's' "
			"to skip step.\n");

	/*
	 * Part 1: Calibrate the lid to base alignment rotation matrix.
	 */
	ccprintf("\nStep 1: close lid, press enter, and rotate the machine\n"
		"in space until all 3 directions are captured.\n");

	/* Wait for user to press enter, quit, or skip. */
	c = wait_for_key();
	if (c == 'q') {
		ccprintf("Calibration exited.\n");
		return EC_SUCCESS;
	}

	/* If step is not skipped, perform calibration. */
	if (c != 's') {
		if (calibrate_orientation(0) != EC_SUCCESS) {
			ccprintf("Calibration error.\n");
			return EC_SUCCESS;
		}
	}

	/*
	 * Part 2: Calibrate the hinge 90 rotation matrix.
	 */
	ccprintf("\nStep 2: open lid to 90 degrees, press enter, and rotate\n"
		"in space until all 3 directions are captured.\n");


	/* Wait for user to press enter, quit, or skip. */
	c = wait_for_key();
	if (c == 'q') {
		ccprintf("Calibration exited.\n");
		return EC_SUCCESS;
	}

	/* If step is not skipped, perform calibration. */
	if (c != 's') {
		if (calibrate_orientation(1) != EC_SUCCESS) {
			ccprintf("Calibration error.\n");
			return EC_SUCCESS;
		}
	}

	/*
	 * Part 3: Calibrate the hinge axis and hinge 180 rotation matrix.
	 */
	ccprintf("\nStep 3: align hinge with gravity, and press enter.\n");

	/* Wait for user to press enter, quit, or skip. */
	c = wait_for_key();
	if (c == 'q') {
		ccprintf("Calibration exited.\n");
		return EC_SUCCESS;
	}

	/* If step is not skipped, perform calibration. */
	if (c != 's') {
		if (calibrate_hinge() != EC_SUCCESS) {
			ccprintf("Calibration error.\n");
			return EC_SUCCESS;
		}
	}

	/*
	 * Part 4: Calibrate the standard reference frame rotation matrix.
	 */
	ccprintf("\nStep 4a: place machine on right side, with hinge\n"
		"aligned with gravity, and press enter.\n");

	/* Wait for user to press enter, quit, or skip. */
	c = wait_for_key();
	if (c == 'q') {
		ccprintf("Calibration exited.\n");
		return EC_SUCCESS;
	}

	if (c == 's')
		goto auto_calib_done;

	/* In this orientation, the Y axis should be highest. Capture data. */
	motion_get_accel_base(&v_y);

	ccprintf("\nStep 4b: place machine flat on table, with keyboard\n"
		"up, and press enter.\n");

	/* Wait for user to press enter, quit, or skip. */
	c = wait_for_key();
	if (c == 'q') {
		ccprintf("Calibration exited.\n");
		return EC_SUCCESS;
	}

	if (c == 's')
		goto auto_calib_done;

	/* In this orientation, the Z axis should be highest. Capture data. */
	motion_get_accel_base(&v_z);

	ccprintf("\nStep 4c: hold machine perpendicular to table with\n"
		"the hinge up, and press enter.\n");

	/* Wait for user to press enter, quit, or skip. */
	c = wait_for_key();
	if (c == 'q') {
		ccprintf("Calibration exited.\n");
		return EC_SUCCESS;
	}

	if (c == 's')
		goto auto_calib_done;

	/* In this orientation, the X axis should be highest. Capture data. */
	motion_get_accel_base(&v_x);

	if (calibrate_standard_frame(&v_x, &v_y, &v_z) != EC_SUCCESS) {
		ccprintf("Calibration error.\n");
		return EC_SUCCESS;
	}

auto_calib_done:
	/* Print results of all calibration. */
	command_print_orientation(0, NULL);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(accelcalib, command_auto_calibrate,
	"",
	"Auto calibrate the accelerometers", NULL);
