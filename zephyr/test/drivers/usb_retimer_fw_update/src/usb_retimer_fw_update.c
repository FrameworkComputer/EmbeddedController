/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_common.h"
#include "usb_pd.h"

#include <zephyr/ztest.h>

#define BB_RETIMER_NODE DT_NODELABEL(usb_c1_bb_retimer_emul)
#define TEST_PORT USBC_PORT_C1

/* Note: for API details, see common/usbc/usb_retimer_fw_update.c */

/* Helpers */
static uint8_t acpi_read_and_verify(void)
{
	uint8_t read_result = acpi_read(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE);

	zassert_not_equal(read_result, USB_RETIMER_FW_UPDATE_ERR,
			  "Command returned unexpected err");
	zassert_not_equal(read_result, USB_RETIMER_FW_UPDATE_INVALID_MUX,
			  "Command returned invalid mux");

	return read_result;
}

static void usb_retimer_fw_update_suspend_port(void)
{
	/* Write our command to suspend the port first */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_SUSPEND_PD
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to suspend port */
	k_sleep(K_SECONDS(1));

	zassert_true(acpi_read_and_verify() == 0,
		     "Failed to see successful suspend");
}

/* Test configuration */
static void usb_retimer_fw_update_before(void *data)
{
	ARG_UNUSED(data);

	/* Assume our common setup of a BB retimer on C1 */
	zassert_true(EMUL_DT_GET(BB_RETIMER_NODE) != NULL,
		     "No BB retimer found on C1");

	/* Set chipset to ON, since AP would drive this process */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void usb_retimer_fw_update_after(void *data)
{
	ARG_UNUSED(data);

	/* Unsuspend the port */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_DISCONNECT
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Port should resume after at least 7 seconds */
	k_sleep(K_SECONDS(8));
}

ZTEST_SUITE(usb_retimer_fw_update, drivers_predicate_post_main, NULL,
	    usb_retimer_fw_update_before, usb_retimer_fw_update_after, NULL);

ZTEST(usb_retimer_fw_update, test_query_port)
{
	/* Write our command to query ports */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_QUERY_PORT
			   << USB_RETIMER_FW_UPDATE_OP_SHIFT);

	zassert_true(acpi_read_and_verify() & BIT(TEST_PORT),
		     "Failed to see port in query");
}

ZTEST(usb_retimer_fw_update, test_suspend_port)
{
	/* Write our command to suspend the port */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_SUSPEND_PD
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to suspend port */
	k_sleep(K_SECONDS(1));

	/* Return of 0 indicates the command succeeded */
	zassert_true(acpi_read_and_verify() == 0,
		     "Failed to see successful suspend");
}

ZTEST(usb_retimer_fw_update, test_resume_port)
{
	usb_retimer_fw_update_suspend_port();

	/* And now resume it */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_RESUME_PD
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to resume port */
	k_sleep(K_SECONDS(1));

	/* Note: return indicates whether the port is enabled */
	zassert_true(acpi_read_and_verify() == 1,
		     "Failed to see successful resume");
}

ZTEST(usb_retimer_fw_update, test_get_mux)
{
	struct ec_response_typec_status typec_status;

	/* Write our command to get the mux state for a port */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_GET_MUX
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to process */
	k_sleep(K_SECONDS(1));

	typec_status = host_cmd_typec_status(TEST_PORT);
	zassert_true(acpi_read_and_verify() == typec_status.mux_state,
		     "Failed to match mux state");
}

/* Commands which first require suspend to be run */
ZTEST(usb_retimer_fw_update, test_set_mux_usb)
{
	struct ec_response_typec_status typec_status;

	usb_retimer_fw_update_suspend_port();

	/* And now set the mux to USB */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_SET_USB
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to set the mux */
	k_sleep(K_SECONDS(1));

	/* Note: return indicates filtered mux state */
	zassert_true(acpi_read_and_verify() == USB_PD_MUX_USB_ENABLED,
		     "Failed to set mux usb");

	typec_status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(typec_status.mux_state & USB_RETIMER_FW_UPDATE_MUX_MASK,
		      USB_PD_MUX_USB_ENABLED, "Status mux disagreement");
}

ZTEST(usb_retimer_fw_update, test_set_mux_safe)
{
	struct ec_response_typec_status typec_status;

	usb_retimer_fw_update_suspend_port();

	/* And now set the mux to safe */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_SET_SAFE
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to set the mux */
	k_sleep(K_SECONDS(1));

	/* Note: return indicates filtered mux state */
	zassert_true(acpi_read_and_verify() == USB_PD_MUX_SAFE_MODE,
		     "Failed to set mux safe");

	typec_status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(typec_status.mux_state & USB_RETIMER_FW_UPDATE_MUX_MASK,
		      USB_PD_MUX_SAFE_MODE, "Status mux disagreement");
}

ZTEST(usb_retimer_fw_update, test_set_mux_tbt)
{
	struct ec_response_typec_status typec_status;

	usb_retimer_fw_update_suspend_port();

	/* And now set the mux to TBT */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_SET_TBT
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to set the mux */
	k_sleep(K_SECONDS(1));

	/* Note: return indicates filtered mux state */
	zassert_true(acpi_read_and_verify() == USB_PD_MUX_TBT_COMPAT_ENABLED,
		     "Failed to set mux tbt");

	typec_status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(typec_status.mux_state & USB_RETIMER_FW_UPDATE_MUX_MASK,
		      USB_PD_MUX_TBT_COMPAT_ENABLED, "Status mux disagreement");
}

ZTEST(usb_retimer_fw_update, test_update_disconnect)
{
	uint64_t command_start;

	usb_retimer_fw_update_suspend_port();

	/* And now set the process to disconnect */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_DISCONNECT
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);
	command_start = k_uptime_get();

	/* Give PD task time to set the mux */
	k_sleep(K_SECONDS(1));

	/* Note: return indicates filtered mux state */
	zassert_true(acpi_read_and_verify() == USB_PD_MUX_NONE,
		     "Failed to set mux disconnect");

	/*
	 * Note: this would ideally be a host command interface check, but
	 * the only HC return which would cover this is a state string, which
	 * could be brittle.
	 */
	/* Port shouldn't be up or at least 5 seconds */
	for (int i = 0; i < 10; i++) {
		if (pd_is_port_enabled(TEST_PORT)) {
			zassert_true((k_uptime_get() - command_start) > 5000,
				     "Port resumed too soon");
			break;
		}
		k_sleep(K_SECONDS(1));
	}

	zassert_true(pd_is_port_enabled(TEST_PORT), "Port not resuemd");
}

/* Verify we get an error if port isn't suspended */
ZTEST(usb_retimer_fw_update, test_mux_usb_error)
{
	/* Set the mux to USB  on unsuspended port */
	acpi_write(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE,
		   USB_RETIMER_FW_UPDATE_SET_USB
				   << USB_RETIMER_FW_UPDATE_OP_SHIFT |
			   TEST_PORT);

	/* Give PD task time to set the mux */
	k_sleep(K_SECONDS(1));

	zassert_true(acpi_read(EC_ACPI_MEM_USB_RETIMER_FW_UPDATE) ==
			     USB_RETIMER_FW_UPDATE_ERR,
		     "Failed to fail mux set");
}
