/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "lpc.h"
#include "ec_commands.h"
#include "pwm.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)

static uint8_t acpi_cmd;         /* Last received ACPI command */
static uint8_t acpi_addr;        /* First byte of data after ACPI command */
static int acpi_data_count;      /* Number of data writes after command */
static uint8_t acpi_mem_test;    /* Test byte in ACPI memory space */

/* This handles AP writes to the EC via the ACPI I/O port. There are only a few
 * ACPI commands (EC_CMD_ACPI_*), but they are all handled here.
 */
int acpi_ap_to_ec(int is_cmd, uint8_t value, uint8_t *resultptr)
{
	int data = 0;
	int retval = 0;

	CPRINTF("[HEY: %T %s(%d, 0x%x)]\n", is_cmd, value);

	/* Read command/data; this clears the FRMH status bit. */
	if (is_cmd) {
		acpi_cmd = value;
		acpi_data_count = 0;
	} else {
		data = value;
		/*
		 * The first data byte is the ACPI memory address for
		 * read/write commands.
		 */
		if (!acpi_data_count++)
			acpi_addr = data;
	}

	/* Process complete commands */
	if (acpi_cmd == EC_CMD_ACPI_READ && acpi_data_count == 1) {
		/* ACPI read cmd + addr */
		int result = 0xff;		/* value for bogus read */

		switch (acpi_addr) {
		case EC_ACPI_MEM_VERSION:
			result = EC_ACPI_MEM_VERSION_CURRENT;
			break;
		case EC_ACPI_MEM_TEST:
			result = acpi_mem_test;
			break;
		case EC_ACPI_MEM_TEST_COMPLIMENT:
			result = 0xff - acpi_mem_test;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			/*
			 * TODO(crosbug.com/p/23774): not very satisfying that
			 * LPC knows directly about the keyboard backlight, but
			 * for now this is good enough and less code than
			 * defining a new API for ACPI commands.  If we start
			 * adding more commands, or need to support LPC on more
			 * than just LM4, fix this.
			 */
			result = pwm_get_duty(PWM_CH_KBLIGHT);
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			/** TODO(crosbug.com/p/23774): Fix this too */
			result = dptf_get_fan_duty_target();
			break;
#endif
		default:
			break;
		}

		/* Send the result byte */
		CPRINTF("[%T ACPI read 0x%02x = 0x%02x]\n", acpi_addr, result);
		*resultptr = result;
		retval = 1;

	} else if (acpi_cmd == EC_CMD_ACPI_WRITE && acpi_data_count == 2) {
		/* ACPI write cmd + addr + data */
		switch (acpi_addr) {
		case EC_ACPI_MEM_TEST:
			CPRINTF("[%T ACPI mem test 0x%02x]\n", data);
			acpi_mem_test = data;
			break;
#ifdef CONFIG_PWM_KBLIGHT
		case EC_ACPI_MEM_KEYBOARD_BACKLIGHT:
			/*
			 * Debug output with CR not newline, because the host
			 * does a lot of keyboard backlights and it scrolls the
			 * debug console.
			 */
			CPRINTF("\r[%T ACPI kblight %d]", data);
			pwm_set_duty(PWM_CH_KBLIGHT, data);
			break;
#endif
#ifdef CONFIG_FANS
		case EC_ACPI_MEM_FAN_DUTY:
			/** TODO(crosbug.com/p/23774): Fix this too */
			dptf_set_fan_duty_target(data);
			break;
#endif
		default:
			CPRINTF("[%T ACPI write 0x%02x = 0x%02x]\n",
				acpi_addr, data);
			break;
		}

/* At the moment, ACPI implies LPC. */
#ifdef CONFIG_LPC
	} else if (acpi_cmd == EC_CMD_ACPI_QUERY_EVENT && !acpi_data_count) {
		/* Clear and return the lowest host event */
		int evt_index = lpc_query_host_event_state();
		CPRINTF("[%T ACPI query = %d]\n", evt_index);
		*resultptr = evt_index;
		retval = 1;
#endif
	}

	return retval;
}
