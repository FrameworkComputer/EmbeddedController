/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h> /* nocheck */
#include <zephyr/shell/shell_uart.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "acpi.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "lpc.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "power.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/utils.h"

#define BATTERY_NODE DT_NODELABEL(battery)
#define GPIO_BATT_PRES_ODL_PATH DT_PATH(named_gpios, ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

void test_set_battery_level(int percentage)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	const struct device *battery_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));
	bat = sbat_emul_get_bat_data(emul);

	bat->cap = bat->full_cap * percentage / 100;
	bat->volt = battery_get_info()->voltage_normal;
	bat->design_mv = bat->volt;

	/* Set battery present gpio. */
	zassert_ok(gpio_emul_input_set(battery_gpio_dev,
				       GPIO_BATT_PRES_ODL_PORT, 0),
		   NULL);

	/* We need to wait for the charge task to re-read battery parameters */
	WAIT_FOR(!charge_want_shutdown(), CHARGE_MAX_SLEEP_USEC + 1,
		 k_sleep(K_SECONDS(1)));
}

void test_set_chipset_to_s0(void)
{
	printk("%s: Forcing power on\n", __func__);

	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));

	/*
	 * Make sure that battery is in good condition to
	 * not trigger hibernate in charge_state_v2.c
	 * Set battery voltage to expected value and capacity to 50%. Battery
	 * will not be full and accepts charging, but will not trigger
	 * hibernate. Charge level is set to the default value of an emulator
	 * (emul/emul_smart_battery.c). b/244366201.
	 */
	test_set_battery_level(50);

	/* The easiest way to power on seems to be the shell command. */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "power on"),
		      NULL);

	k_sleep(K_SECONDS(1));

	/* Check if chipset is in correct state */
	zassert_equal(POWER_S0, power_get_state(), "Expected S0, got %d",
		      power_get_state());
}

void test_set_chipset_to_power_level(enum power_state new_state)
{
	zassert_true(new_state == POWER_G3 || new_state == POWER_S5 ||
			     new_state == POWER_S4 || new_state == POWER_S3 ||
			     new_state == POWER_S0
#ifdef CONFIG_POWER_S0IX
			     || new_state == POWER_S0ix
#endif
		     ,
		     "Power state must be one of the steady states");
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));

	if (new_state == POWER_G3) {
		test_set_chipset_to_g3();
		return;
	}

	test_set_chipset_to_s0();

	power_set_state(new_state);

	k_sleep(K_SECONDS(1));

	/* Check if chipset is in correct state */
	zassert_equal(new_state, power_get_state(), "Expected %d, got %d",
		      new_state, power_get_state());
}

void test_set_chipset_to_g3(void)
{
	/* Let power code to settle on a particular state first. */
	task_wake(TASK_ID_CHIPSET);
	k_sleep(K_SECONDS(1));

	printk("%s: Forcing shutdown\n", __func__);
	chipset_force_shutdown(CHIPSET_RESET_KB_SYSRESET);
	k_sleep(K_SECONDS(20));
	/* Check if chipset is in correct state */
	zassert_equal(POWER_G3, power_get_state(), "Expected G3, got %d",
		      power_get_state());
}

void connect_source_to_port(struct tcpci_partner_data *partner,
			    struct tcpci_src_emul_data *src, int pdo_index,
			    const struct emul *tcpci_emul,
			    const struct emul *charger_emul)
{
	set_ac_enabled(true);
	zassume_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul), NULL);

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_GET_VOLT(src->pdo[pdo_index]));

	k_sleep(K_SECONDS(10));
}

void disconnect_source_from_port(const struct emul *tcpci_emul,
				 const struct emul *charger_emul)
{
	set_ac_enabled(false);
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

void connect_sink_to_port(struct tcpci_partner_data *partner,
			  const struct emul *tcpci_emul,
			  const struct emul *charger_emul)
{
	/*
	 * TODO(b/221439302) Updating the TCPCI emulator registers, updating the
	 *   vbus, as well as alerting should all be a part of the connect
	 *   function.
	 */
	/* Enforce that we only support the isl923x emulator for now */
	__ASSERT_NO_MSG(EMUL_DT_GET(DT_NODELABEL(isl923x_emul)) ==
			charger_emul);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	tcpci_tcpc_alert(0);
	k_sleep(K_SECONDS(1));

	zassume_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul), NULL);

	/* Wait for PD negotiation and current ramp.
	 * TODO(b/213906889): Check message timing and contents.
	 */
	k_sleep(K_SECONDS(10));
}

void disconnect_sink_from_port(const struct emul *tcpci_emul)
{
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	k_sleep(K_SECONDS(1));
}

uint8_t acpi_read(uint8_t acpi_addr)
{
	uint8_t readval;
	/*
	 * See ec_commands.h for details on the required process
	 * First, send the read command, which should populate no data
	 */
	zassume_ok(acpi_ap_to_ec(true, EC_CMD_ACPI_READ, &readval),
		   "Failed to send read command");

	/* Next, time for the address which should populate our result */
	zassume_equal(acpi_ap_to_ec(false, acpi_addr, &readval), 1,
		      "Failed to read value");
	return readval;
}

void acpi_write(uint8_t acpi_addr, uint8_t write_byte)
{
	uint8_t readval;
	/*
	 * See ec_commands.h for details on the required process
	 * First, send the read command, which should populate no data
	 */
	zassume_ok(acpi_ap_to_ec(true, EC_CMD_ACPI_WRITE, &readval),
		   "Failed to send read command");

	/* Next, time for the address we want to write */
	zassume_ok(acpi_ap_to_ec(false, acpi_addr, &readval),
		   "Failed to write address");

	/* Finally, time to write the data */
	zassume_ok(acpi_ap_to_ec(false, write_byte, &readval),
		   "Failed to write value");
}

enum ec_status host_cmd_host_event(enum ec_host_event_action action,
				   enum ec_host_event_mask_type mask_type,
				   struct ec_response_host_event *r)
{
	enum ec_status ret_val;

	struct ec_params_host_event params = {
		.action = action,
		.mask_type = mask_type,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_HOST_EVENT, 0, *r, params);

	ret_val = host_command_process(&args);

	return ret_val;
}

void host_cmd_motion_sense_dump(int max_sensor_count,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_DUMP,
		.dump = {
			.max_sensor_count = max_sensor_count,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 4, *response, params);

	zassume_ok(host_command_process(&args),
		   "Failed to get motion_sense dump");
}

int host_cmd_motion_sense_data(uint8_t sensor_num,
			       struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_DATA,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 4, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_info(uint8_t cmd_version, uint8_t sensor_num,
			       struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_INFO,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, cmd_version, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_ec_rate(uint8_t sensor_num, int data_rate_ms,
				  struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_EC_RATE,
		.ec_rate = {
			.sensor_num = sensor_num,
			.data = data_rate_ms,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_odr(uint8_t sensor_num, int32_t odr, bool round_up,
			      struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_ODR,
		.sensor_odr = {
			.sensor_num = sensor_num,
			.data = odr,
			.roundup = round_up,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_range(uint8_t sensor_num, int32_t range,
				bool round_up,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_RANGE,
		.sensor_range = {
			.sensor_num = sensor_num,
			.data = range,
			.roundup = round_up,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_offset(uint8_t sensor_num, uint16_t flags,
				 int16_t temperature, int16_t offset_x,
				 int16_t offset_y, int16_t offset_z,
				 struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_OFFSET,
		.sensor_offset = {
			.sensor_num = sensor_num,
			.flags = flags,
			.temp = temperature,
			.offset = { offset_x, offset_y, offset_z },
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_scale(uint8_t sensor_num, uint16_t flags,
				int16_t temperature, int16_t scale_x,
				int16_t scale_y, int16_t scale_z,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SENSOR_SCALE,
		.sensor_scale = {
			.sensor_num = sensor_num,
			.flags = flags,
			.temp = temperature,
			.scale = { scale_x, scale_y, scale_z },
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_calib(uint8_t sensor_num, bool enable,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_PERFORM_CALIB,
		.perform_calib = {
			.sensor_num = sensor_num,
			.enable = enable,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_flush(uint8_t sensor_num,
				     struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_FLUSH,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_info(struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_INFO,
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_read(uint8_t buffer_length,
				    struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_READ,
		.fifo_read = {
			.max_data_vector = buffer_length,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_int_enable(int8_t enable,
				     struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE,
		.fifo_int_enable = {
			.enable = enable,
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

int host_cmd_motion_sense_spoof(uint8_t sensor_num, uint8_t enable,
				int16_t values0, int16_t values1,
				int16_t values2,
				struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_SPOOF,
		.spoof = {
			.sensor_id = sensor_num,
			.spoof_enable = enable,
			.components = { values0, values1, values2 },
		},
	};
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND(
		EC_CMD_MOTION_SENSE_CMD, 1, *response, params);

	return host_command_process(&args);
}

void host_cmd_typec_discovery(int port, enum typec_partner_type partner_type,
			      void *response, size_t response_size)
{
	struct ec_params_typec_discovery params = {
		.port = port, .partner_type = partner_type
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_DISCOVERY, 0, params);
	/* The expected response to EC_CMD_TYPEC_DISCOVERY extends beyond the
	 * bounds of struct ec_response_typec_discovery.
	 */
	args.response = response;
	args.response_max = response_size;

	zassume_ok(host_command_process(&args),
		   "Failed to get Type-C state for port %d", port);
}

void host_cmd_typec_control_enter_mode(int port, enum typec_mode mode)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_ENTER_MODE,
		.mode_to_enter = mode
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_exit_modes(int port)
{
	struct ec_params_typec_control params = {
		.port = port, .command = TYPEC_CONTROL_COMMAND_EXIT_MODES
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_usb_mux_set(int port,
					struct typec_usb_mux_set mux_set)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_USB_MUX_SET,
		.mux_params = mux_set,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_clear_events(int port, uint32_t events)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_CLEAR_EVENTS,
		.clear_events_mask = events,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_bist_share_mode(int port, int enable)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_BIST_SHARE_MODE,
		.bist_share_mode = enable
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_TYPEC_CONTROL, 0, params);

	zassume_ok(host_command_process(&args),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_usb_pd_get_amode(
	uint8_t port, uint16_t svid_idx,
	struct ec_params_usb_pd_get_mode_response *response, int *response_size)
{
	struct ec_params_usb_pd_get_mode_request params = {
		.port = port,
		.svid_idx = svid_idx,
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_USB_PD_GET_AMODE, 0, params);
	args.response = response;

	zassume_ok(host_command_process(&args),
		   "Failed to get alternate-mode info for port %d", port);
	*response_size = args.response_size;
}

void host_events_save(struct host_events_ctx *host_events_ctx)
{
	host_events_ctx->lpc_host_events = lpc_get_host_events();

	for (int i = 0; i < LPC_HOST_EVENT_COUNT; i++) {
		host_events_ctx->lpc_host_event_mask[i] =
			lpc_get_host_events_by_type(i);
	}
}

void host_events_restore(struct host_events_ctx *host_events_ctx)
{
	lpc_set_host_event_state(host_events_ctx->lpc_host_events);

	for (int i = 0; i < LPC_HOST_EVENT_COUNT; i++) {
		lpc_set_host_event_mask(
			i, host_events_ctx->lpc_host_event_mask[i]);
	}
}

K_HEAP_DEFINE(test_heap, 2048);

void *test_malloc(size_t bytes)
{
	void *mem;

	mem = k_heap_alloc(&test_heap, bytes, K_NO_WAIT);

	if (mem == NULL) {
		printk("Failed to alloc %d bytes\n", bytes);
	}

	return mem;
}

void test_free(void *mem)
{
	k_heap_free(&test_heap, mem);
}

int emul_init_stub(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

/* These 2 lines are needed because we don't define an espi host driver */
#define DT_DRV_COMPAT zephyr_espi_emul_espi_host
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

void check_console_cmd(const char *cmd, const char *expected_output,
		       const int expected_rv, const char *file, const int line)
{
	const char *buffer;
	size_t buffer_size;
	int rv;

	shell_backend_dummy_clear_output(get_ec_shell());
	rv = shell_execute_cmd(get_ec_shell(), cmd);

	zassert_equal(expected_rv, rv,
		      "%s:%u \'%s\' - Expected %d, returned %d", file, line,
		      cmd, expected_rv, rv);

	if (expected_output) {
		buffer = shell_backend_dummy_get_output(get_ec_shell(),
							&buffer_size);
		zassert_true(strstr(buffer, expected_output),
			     "Invalid console output %s", buffer);
	}
}
