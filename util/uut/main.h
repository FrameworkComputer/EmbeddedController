/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_UUT_MAIN_H
#define __UTIL_UUT_MAIN_H

#include <stdbool.h>

/*---------------------------------------------------------------------------
 * Constant definitions
 *---------------------------------------------------------------------------
 */
/* Maximum Read/Write data size per packet */
#define MAX_RW_DATA_SIZE 256

/* Base for string conversion */
#define BASE_DECIMAL 10
#define BASE_HEXADECIMAL 16

/* Verbose control messages display */
#define DISPLAY_MSG(msg) \
{                        \
	if (verbose)         \
		printf msg;      \
}

#define SUCCESS true
#define FAIL false

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------------
 * Global variables
 *--------------------------------------------------------------------------
 */

extern bool verbose;
extern bool console;

/*--------------------------------------------------------------------------
 * Global functions
 *--------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------
 * Function:	display_color_msg
 *
 * Parameters:
 *		success - SUCCESS for successful message, FAIL for erroneous
 *			  massage.
 *		fmt     - Massage to dispaly (format and arguments).
 *
 * Returns:	none
 * Side effects:	Using DISPLAY_MSG macro.
 * Description:
 *	This routine displays a message using color attributes:
 *		In case of a successful message, use green foreground text on
 *		black background.
 *		In case of an erroneous message, use red foreground text on
 *		black background.
 *--------------------------------------------------------------------------
 */
void display_color_msg(bool success, char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_UUT_MAIN_H */
