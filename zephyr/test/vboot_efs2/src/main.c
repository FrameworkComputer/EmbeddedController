/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "zephyr/kernel.h"
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test_new.h>
#include <zephyr/shell/shell_dummy.h> /* nocheck */

#include "ec_app_main.h"
#include "hooks.h"
#include "task.h"
#include "emul/emul_flash.h"
#include "vboot.h"
#include "driver/bc12/pi3usb9201_public.h"
#include "usb_mux.h"
#include "driver/tcpm/tcpci.h"
#include "ppc/sn5s330_public.h"
#include "usbc_ppc.h"

static int show_power_shortage_called;
void show_power_shortage(void)
{
	show_power_shortage_called++;
}

static int show_critical_error_called;
void show_critical_error(void)
{
	show_critical_error_called++;
}

ZTEST(vboot_efs2, test_vboot_main_system_is_in_rw)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	/* Set system_is_in_rw */
	system_set_shrspi_image_copy(EC_IMAGE_RW);

	shell_backend_dummy_clear_output(shell_zephyr); /* nocheck */
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, /* nocheck */
						   &buffer_size);
	zassert_equal(show_power_shortage_called, 1, NULL);

	zassert_true(strstr(outbuffer, "VB Already in RW") != NULL,
		     "Expected msg not in %s", outbuffer);

	/* Verify some things we don't expect also. */
	zassert_true(strstr(outbuffer, "VB Ping Cr50") == NULL,
		     "Unexpected msg in %s", outbuffer);
	zassert_true(strstr(outbuffer, "VB Exit") == NULL,
		     "Unexpected msg in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_critical_error_called, 0, NULL);
}

ZTEST(vboot_efs2, test_vboot_main_system_is_manual_recovery)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	system_enter_manual_recovery();

	shell_backend_dummy_clear_output(shell_zephyr); /* nocheck */
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, /* nocheck */
						   &buffer_size);
	zassert_equal(show_power_shortage_called, 0, NULL);
	zassert_true(strstr(outbuffer, "VB In recovery mode") != NULL,
		     "Expected msg not in %s", outbuffer);

	/* Verify some things we don't expect also. */
	zassert_true(strstr(outbuffer, "VB Ping Cr50") == NULL,
		     "Unexpected msg in %s", outbuffer);
	zassert_true(strstr(outbuffer, "VB Exit") == NULL,
		     "Unexpected msg in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_critical_error_called, 0, NULL);
}

ZTEST(vboot_efs2, test_vboot_main_stay_in_ro)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	system_set_reset_flags(EC_RESET_FLAG_STAY_IN_RO);

	shell_backend_dummy_clear_output(shell_zephyr); /* nocheck */
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, /* nocheck */
						   &buffer_size);
	zassert_equal(show_power_shortage_called, 0, NULL);

	/* Verify some things we don't expect also. */
	zassert_true(strstr(outbuffer, "VB In recovery mode") == NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_true(strstr(outbuffer, "VB Ping Cr50") == NULL,
		     "Unexpected msg in %s", outbuffer);
	zassert_true(strstr(outbuffer, "VB Exit") == NULL,
		     "Unexpected msg in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_critical_error_called, 0, NULL);
}

ZTEST(vboot_efs2, test_vboot_main_jump_timeout)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;

	shell_backend_dummy_clear_output(shell_zephyr); /* nocheck */
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, /* nocheck */
						   &buffer_size);
	zassert_equal(show_critical_error_called, 1, NULL);

	zassert_true(strstr(outbuffer, "VB Ping Cr50") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_power_shortage_called, 0, NULL);
}

/* TODO(jbettis): Add cases for verify_and_jump() CR50_COMM_ERR_BAD_PAYLOAD &
 * CR50_COMM_SUCCESS
 */

void *vboot_efs2_setup(void)
{
	/* Wait for the shell to start. */
	k_sleep(K_MSEC(1));
	zassert_equal(get_ec_shell()->ctx->state, SHELL_STATE_ACTIVE, NULL);
	return NULL;
}

void vboot_efs2_cleanup(void *fixture)
{
	ARG_UNUSED(fixture);

	system_set_shrspi_image_copy(EC_IMAGE_RO);
	show_power_shortage_called = 0;
	show_critical_error_called = 0;
	system_exit_manual_recovery();
	system_clear_reset_flags(EC_RESET_FLAG_STAY_IN_RO);
	vboot_disable_pd();
}

ZTEST_SUITE(vboot_efs2, NULL, vboot_efs2_setup, NULL, vboot_efs2_cleanup, NULL);

int board_set_active_charge_port(int port)
{
	return EC_ERROR_INVAL;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
}

void pd_power_supply_reset(int port)
{
}

int pd_check_vconn_swap(int port)
{
	return 0;
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_COUNT };

/* BC1.2 charger detect configuration */
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = DT_REG_ADDR(DT_NODELABEL(tcpci_emul)),
	},
};

/* USBC PPC configuration */
struct ppc_config_t ppc_chips[] = {
	[USBC_PORT_C0] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = SN5S330_ADDR0_FLAGS,
		.drv = &sn5s330_drv,
	},
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);
