/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/bc12/pi3usb9201_public.h"
#include "driver/tcpm/tcpci.h"
#include "ec_app_main.h"
#include "emul/emul_flash.h"
#include "hooks.h"
#include "ppc/sn5s330_public.h"
#include "system_fake.h"
#include "task.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "vboot.h"
#include "zephyr/devicetree.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/uart/serial_test.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#define SERIAL_BUFFER_SIZE DT_PROP(DT_NODELABEL(test_uart), buffer_size)

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
	const struct ec_params_reboot_ec *cmd = system_get_reboot_at_shutdown();

	/* Set system_is_in_rw */
	system_set_shrspi_image_copy(EC_IMAGE_RW);

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_equal(show_power_shortage_called, 1, NULL);

	zassert_true(strstr(outbuffer, "VB Already in RW") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_equal(cmd->cmd, 0);
	zassert_equal(cmd->flags, 0);

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
	const struct ec_params_reboot_ec *cmd = system_get_reboot_at_shutdown();

	system_enter_manual_recovery();

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_equal(show_power_shortage_called, 0, NULL);
	zassert_true(
		strstr(outbuffer,
		       "VB Recovery mode. Scheduled reboot on shutdown.") !=
			NULL,
		"Expected msg not in %s", outbuffer);
	zassert_equal(cmd->cmd, EC_REBOOT_COLD);
	zassert_equal(cmd->flags, 0);

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

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
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

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_equal(show_critical_error_called, 1, NULL);

	zassert_true(strstr(outbuffer, "VB Ping Cr50") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_power_shortage_called, 0, NULL);
}

#define PACKET_MODE_GPIO NAMED_GPIOS_GPIO_NODE(ec_gsc_packet_mode)

static const struct device *uart_shell_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
static const struct device *gpio_dev =
	DEVICE_DT_GET(DT_GPIO_CTLR(PACKET_MODE_GPIO, gpios));

static void reply_cr50_payload(const struct device *dev, void *user_data)
{
	if (gpio_emul_output_get(gpio_dev,
				 DT_GPIO_PIN(PACKET_MODE_GPIO, gpios))) {
		struct cr50_comm_request req;
		uint32_t bytes_read;

		bytes_read = serial_vnd_peek_out_data(
			uart_shell_dev, (void *)&req, sizeof(req));
		/* If ! valid cr50_comm_request header, read 1 byte. */
		while (bytes_read == sizeof(req) &&
		       req.magic != CR50_PACKET_MAGIC) {
			/* Consume one byte and then peek again. */
			serial_vnd_read_out_data(uart_shell_dev, NULL, 1);
			bytes_read = serial_vnd_peek_out_data(
				uart_shell_dev, (void *)&req, sizeof(req));
		}
		if (bytes_read == sizeof(req)) {
			/* If we have a full packet, consume it, and reply
			 * with whatever is in user_data which holds a cr50
			 * reply.
			 */
			if (req.size + sizeof(req) <=
			    serial_vnd_out_data_size_get(uart_shell_dev)) {
				serial_vnd_read_out_data(uart_shell_dev, NULL,
							 req.size +
								 sizeof(req));
				serial_vnd_queue_in_data(
					uart_shell_dev, user_data,
					sizeof(struct cr50_comm_response));
			}
		}
	} else {
		/* Packet mode is off, so just consume enough bytes from the out
		 * buffer to clear it.
		 */
		serial_vnd_read_out_data(uart_shell_dev, NULL,
					 SERIAL_BUFFER_SIZE);
	}
}

ZTEST(vboot_efs2, test_vboot_main_jump_bad_payload)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	struct cr50_comm_response resp = {
		.error = CR50_COMM_ERR_BAD_PAYLOAD,
	};

	serial_vnd_set_callback(uart_shell_dev, reply_cr50_payload, &resp);

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(strstr(outbuffer, "VB Ping Cr50") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_true(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_power_shortage_called, 0, NULL);
	zassert_equal(show_critical_error_called, 0, NULL);
}

/* This hits the default case in verify_and_jump. */
ZTEST(vboot_efs2, test_vboot_main_jump_bad_crc)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	struct cr50_comm_response resp = {
		.error = CR50_COMM_ERR_CRC,
	};

	serial_vnd_set_callback(uart_shell_dev, reply_cr50_payload, &resp);

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(strstr(outbuffer, "VB Ping Cr50") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_true(strstr(outbuffer, "VB Failed to verify RW (0xec03)") !=
			     NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_power_shortage_called, 0, NULL);
	zassert_equal(show_critical_error_called, 1, NULL);
}

ZTEST(vboot_efs2, test_vboot_main_vboot_get_rw_hash_fail)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	struct ec_response_vboot_hash response;
	struct ec_params_vboot_hash hash_start_params = {
		.cmd = EC_VBOOT_HASH_START,
		.hash_type = EC_VBOOT_HASH_TYPE_SHA256,
		.offset = 0,
		.size = 0x12345,
	};

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(ec_cmd_vboot_hash(NULL, &hash_start_params, &response),
		   NULL);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(strstr(outbuffer, "VB Ping Cr50") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_true(strstr(outbuffer, "VB Failed to verify RW (0x6)") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_power_shortage_called, 0, NULL);
	zassert_equal(show_critical_error_called, 1, NULL);
}

ZTEST(vboot_efs2, test_vboot_main_jump_success)
{
	const struct shell *shell_zephyr = get_ec_shell();
	const char *outbuffer;
	size_t buffer_size;
	struct cr50_comm_response resp = {
		.error = CR50_COMM_SUCCESS,
	};

	serial_vnd_set_callback(uart_shell_dev, reply_cr50_payload, &resp);

	shell_backend_dummy_clear_output(shell_zephyr);
	vboot_main();

	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);

	zassert_true(strstr(outbuffer, "VB Ping Cr50") != NULL,
		     "Expected msg not in %s", outbuffer);
	zassert_false(vboot_allow_usb_pd(), NULL);
	zassert_equal(show_power_shortage_called, 0, NULL);
	zassert_equal(show_critical_error_called, 1, NULL);
	zassert_equal(system_get_reset_flags(), 0, NULL);
}

void *vboot_efs2_setup(void)
{
	/* Wait for the shell to start. */
	k_sleep(K_MSEC(1));
	zassert_equal(get_ec_shell()->ctx->state, SHELL_STATE_ACTIVE, NULL);

	system_common_pre_init();

	return NULL;
}

void vboot_efs2_cleanup(void *fixture)
{
	ARG_UNUSED(fixture);

	system_set_shrspi_image_copy(EC_IMAGE_RO);
	show_power_shortage_called = 0;
	show_critical_error_called = 0;
	system_exit_manual_recovery();
	system_clear_reset_flags(EC_RESET_FLAG_STAY_IN_RO | EC_RESET_FLAG_EFS |
				 EC_RESET_FLAG_AP_IDLE);
	vboot_disable_pd();
	serial_vnd_set_callback(uart_shell_dev, NULL, NULL);
	serial_vnd_read_out_data(uart_shell_dev, NULL, SERIAL_BUFFER_SIZE);
}

ZTEST_SUITE(vboot_efs2, NULL, vboot_efs2_setup, NULL, vboot_efs2_cleanup, NULL);

int board_set_active_charge_port(int port)
{
	return EC_ERROR_INVAL;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
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

enum usbc_port { USBC_PORT_COUNT };
