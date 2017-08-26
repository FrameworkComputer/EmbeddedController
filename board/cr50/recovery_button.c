/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Recovery button override module. */

#include "common.h"
#include "console.h"
#include "extension.h"
#include "registers.h"
#include "util.h"

/*
 * The recovery button, on some systems only, is wired to KEY0 in rbox.  For
 * testing, we need to be able override the value.  We'll have a vendor command
 * such that the AP can query the state of the recovery button.  However, the
 * reported state can only be overridden with a console command given sufficient
 * privileges.
 */
static uint8_t rec_btn_force_pressed;

static uint8_t is_rec_btn_pressed(void)
{
	if (rec_btn_force_pressed)
		return 1;

	/*
	 * If not force pressed, check the actual state of button.  Note,
	 * the value is inverted because the button is active low.
	 */
	return !GREAD_FIELD(RBOX, CHECK_INPUT, KEY0_IN);
}

static int command_recbtnforce(int argc, char **argv)
{
	int val;

	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	if (argc == 2) {
		/* Make sure we're allowed to override the recovery button. */
		if (console_is_restricted())
			return EC_ERROR_ACCESS_DENIED;

		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		rec_btn_force_pressed = val;
	}

	ccprintf("RecBtn: %s pressed\n",
		 rec_btn_force_pressed ? "forced" :
		 is_rec_btn_pressed() ? "" : "not");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(recbtnforce, command_recbtnforce,
			     "[enable | disable]",
			     "Force enable the reported recbtn state.");

static enum vendor_cmd_rc vc_get_rec_btn(enum vendor_cmd_cc code,
					 void *buf,
					 size_t input_size,
					 size_t *response_size)
{
	*(uint8_t *)buf = is_rec_btn_pressed();
	*response_size = 1;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_REC_BTN, vc_get_rec_btn);
