/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MKBP info host command */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_mkbp.h"
#include "keyboard_scan.h"
#include "mkbp_info.h"
#include "mkbp_input_devices.h"
#include "util.h"

__overridable int mkbp_support_volume_buttons(void)
{
#ifdef CONFIG_VOLUME_BUTTONS
	return 1;
#else
	return 0;
#endif
}

test_export_static uint32_t get_supported_buttons(void)
{
	uint32_t val = 0;

	if (mkbp_support_volume_buttons()) {
		val |= BIT(EC_MKBP_VOL_UP) | BIT(EC_MKBP_VOL_DOWN);
	}

#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
	val |= BIT(EC_MKBP_RECOVERY);
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */

#ifdef CONFIG_POWER_BUTTON
	val |= BIT(EC_MKBP_POWER_BUTTON);
#endif /* defined(CONFIG_POWER_BUTTON) */

	return val;
}

test_export_static uint32_t get_supported_switches(void)
{
	uint32_t val = 0;

#ifdef CONFIG_LID_SWITCH
	val |= BIT(EC_MKBP_LID_OPEN);
#endif
#ifdef CONFIG_TABLET_MODE_SWITCH
	val |= BIT(EC_MKBP_TABLET_MODE);
#endif
#ifdef CONFIG_BASE_ATTACHED_SWITCH
	val |= BIT(EC_MKBP_BASE_ATTACHED);
#endif
#ifdef CONFIG_FRONT_PROXIMITY_SWITCH
	val |= BIT(EC_MKBP_FRONT_PROXIMITY);
#endif
	return val;
}

static enum ec_status mkbp_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_info *p = args->params;

	if (args->params_size == 0 || p->info_type == EC_MKBP_INFO_KBD) {
		struct ec_response_mkbp_info *r = args->response;

#ifdef CONFIG_KEYBOARD_PROTOCOL_MKBP
		/* Version 0 just returns info about the keyboard. */
		r->rows = KEYBOARD_ROWS;
		r->cols = keyboard_cols;
#else
		r->rows = 0;
		r->cols = 0;
#endif /* CONFIG_KEYBOARD_PROTOCOL_MKBP */

		/* This used to be "switches" which was previously 0. */
		r->reserved = 0;

		args->response_size = sizeof(struct ec_response_mkbp_info);
	} else {
		union ec_response_get_next_data *r = args->response;

		/* Version 1 (other than EC_MKBP_INFO_KBD) */
		switch (p->info_type) {
		case EC_MKBP_INFO_SUPPORTED:
			switch (p->event_type) {
			case EC_MKBP_EVENT_BUTTON:
				r->buttons = get_supported_buttons();
				args->response_size = sizeof(r->buttons);
				break;

			case EC_MKBP_EVENT_SWITCH:
				r->switches = get_supported_switches();
				args->response_size = sizeof(r->switches);
				break;

			default:
				/* Don't care for now for other types. */
				return EC_RES_INVALID_PARAM;
			}
			break;

		case EC_MKBP_INFO_CURRENT:
			switch (p->event_type) {
#ifdef HAS_TASK_KEYSCAN
			case EC_MKBP_EVENT_KEY_MATRIX:
				memcpy(r->key_matrix, keyboard_scan_get_state(),
				       sizeof(r->key_matrix));
				args->response_size = sizeof(r->key_matrix);
				break;
#endif
			case EC_MKBP_EVENT_HOST_EVENT:
				r->host_event = (uint32_t)host_get_events();
				args->response_size = sizeof(r->host_event);
				break;

			case EC_MKBP_EVENT_HOST_EVENT64:
				r->host_event64 = host_get_events();
				args->response_size = sizeof(r->host_event64);
				break;

#ifdef CONFIG_MKBP_INPUT_DEVICES
			case EC_MKBP_EVENT_BUTTON:
				r->buttons = mkbp_get_button_state();
				args->response_size = sizeof(r->buttons);
				break;

			case EC_MKBP_EVENT_SWITCH:
				r->switches = mkbp_get_switch_state();
				args->response_size = sizeof(r->switches);
				break;
#endif /* CONFIG_MKBP_INPUT_DEVICES */

			default:
				/* Doesn't make sense for other event types. */
				return EC_RES_INVALID_PARAM;
			}
			break;

		default:
			/* Unsupported query. */
			return EC_RES_ERROR;
		}
	}
	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_MKBP_INFO, mkbp_get_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
