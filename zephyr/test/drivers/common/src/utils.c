/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "acpi.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_state.h"
#include "chipset.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "hooks.h"
#include "lpc.h"
#include "power.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/simulator.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/ztest.h>

#define BATTERY_NODE DT_NODELABEL(battery)
#define GPIO_BATT_PRES_ODL_PATH NAMED_GPIOS_GPIO_NODE(ec_batt_pres_odl)
#define GPIO_BATT_PRES_ODL_PORT DT_GPIO_PIN(GPIO_BATT_PRES_ODL_PATH, gpios)

void test_set_battery_level(int percentage)
{
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	const struct device *battery_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_BATT_PRES_ODL_PATH, gpios));
	bat = sbat_emul_get_bat_data(emul);

	bat->cap = bat->full_cap * percentage / 100;
	init_battery_type();
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
	 * not trigger hibernate in charge_state.c
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

#if DT_HAS_COMPAT_STATUS_OKAY(cros_isl923x_emul)
void connect_source_to_port(struct tcpci_partner_data *partner,
			    struct tcpci_src_emul_data *src, int pdo_index,
			    const struct emul *tcpci_emul,
			    const struct emul *charger_emul)
{
	set_ac_enabled(true);
	zassert_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul));

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_GET_VOLT(src->pdo[pdo_index]));

	k_sleep(K_SECONDS(10));
}

void disconnect_source_from_port(const struct emul *tcpci_emul,
				 const struct emul *charger_emul)
{
	set_ac_enabled(false);
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul));
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
	zassert_ok(tcpci_emul_set_vbus_level(tcpci_emul, VBUS_SAFE0V));
	tcpci_tcpc_alert(0);
	k_sleep(K_SECONDS(1));

	zassert_ok(tcpci_partner_connect_to_tcpci(partner, tcpci_emul));

	/* Wait for PD negotiation and current ramp.
	 * TODO(b/213906889): Check message timing and contents.
	 */
	k_sleep(K_SECONDS(10));
}

void disconnect_sink_from_port(const struct emul *tcpci_emul)
{
	zassert_ok(tcpci_emul_disconnect_partner(tcpci_emul));
	k_sleep(K_SECONDS(1));
}
#endif /* DT_HAS_COMPAT_STATUS_OKAY(cros_isl923x_emul) */

uint8_t acpi_read(uint8_t acpi_addr)
{
	uint8_t readval;
	/*
	 * See ec_commands.h for details on the required process
	 * First, send the read command, which should populate no data
	 */
	zassert_ok(acpi_ap_to_ec(true, EC_CMD_ACPI_READ, &readval),
		   "Failed to send read command");

	/* Next, time for the address which should populate our result */
	zassert_equal(acpi_ap_to_ec(false, acpi_addr, &readval), 1,
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
	zassert_ok(acpi_ap_to_ec(true, EC_CMD_ACPI_WRITE, &readval),
		   "Failed to send read command");

	/* Next, time for the address we want to write */
	zassert_ok(acpi_ap_to_ec(false, acpi_addr, &readval),
		   "Failed to write address");

	/* Finally, time to write the data */
	zassert_ok(acpi_ap_to_ec(false, write_byte, &readval),
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

	ret_val = ec_cmd_host_event(NULL, &params, r);

	return ret_val;
}

void host_cmd_motion_sense_dump(int max_sensor_count,
				struct ec_response_motion_sense *response,
				size_t response_size)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_DUMP,
		.dump = {
			.max_sensor_count = max_sensor_count,
		},
	};

	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_MOTION_SENSE_CMD,
		.version = 4,
		.params = &params,
		.params_size = sizeof(params),
		.response = response,
		.response_max = response_size,
		.response_size = 0,
	};

	zassert_ok(host_command_process(&args),
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

	return ec_cmd_motion_sense_cmd_v4(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
}

int host_cmd_motion_sense_fifo_flush(uint8_t sensor_num,
				     struct ec_response_motion_sense *response,
				     size_t response_size)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_FLUSH,
		.sensor_odr = {
			.sensor_num = sensor_num,
		},
	};

	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_MOTION_SENSE_CMD,
		.version = 1,
		.params = &params,
		.params_size = sizeof(params),
		.response = response,
		.response_max = response_size,
		.response_size = 0,
	};

	return host_command_process(&args);
}

int host_cmd_motion_sense_fifo_info(struct ec_response_motion_sense *response,
				    size_t response_size)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_FIFO_INFO,
	};

	struct host_cmd_handler_args args = {
		.send_response = stub_send_response_callback,
		.command = EC_CMD_MOTION_SENSE_CMD,
		.version = 1,
		.params = &params,
		.params_size = sizeof(params),
		.response = response,
		.response_max = response_size,
		.response_size = 0,
	};

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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
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

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
}

int host_cmd_motion_sense_kb_wake_angle(
	int16_t data, struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_KB_WAKE_ANGLE,
		.kb_wake_angle = {
			.data = data,
		},
	};

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
}

int host_cmd_motion_sense_lid_angle(struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense params = {
		.cmd = MOTIONSENSE_CMD_LID_ANGLE,
	};

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
}

int host_cmd_motion_sense_tablet_mode_lid_angle(
	int16_t lid_angle, int16_t hys_degree,
	struct ec_response_motion_sense *response)
{
	struct ec_params_motion_sense
		params = { .cmd = MOTIONSENSE_CMD_TABLET_MODE_LID_ANGLE,
			   .tablet_mode_threshold = {
				   .lid_angle = lid_angle,
				   .hys_degree = hys_degree,
			   } };

	return ec_cmd_motion_sense_cmd_v1(NULL, &params, response);
}

int host_cmd_cec_set(int port, enum cec_command cmd, uint8_t val)
{
	struct ec_params_cec_set params = {
		.cmd = cmd,
		.port = port,
		.val = val,
	};

	return ec_cmd_cec_set(NULL, &params);
}

int host_cmd_cec_get(int port, enum cec_command cmd,
		     struct ec_response_cec_get *response)
{
	struct ec_params_cec_get params = {
		.cmd = cmd,
		.port = port,
	};

	return ec_cmd_cec_get(NULL, &params, response);
}

int host_cmd_cec_write(const uint8_t *msg, uint8_t msg_len)
{
	struct ec_params_cec_write params;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_PARAMS(EC_CMD_CEC_WRITE_MSG, 0, params);

	memcpy(params.msg, msg, MIN(msg_len, sizeof(params.msg)));
	args.params_size = msg_len;

	return host_command_process(&args);
}

int host_cmd_cec_write_v1(int port, const uint8_t *msg, uint8_t msg_len)
{
	struct ec_params_cec_write_v1 params_v1;

	params_v1.port = port;
	params_v1.msg_len = msg_len;
	memcpy(params_v1.msg, msg, MIN(msg_len, sizeof(params_v1.msg)));

	return ec_cmd_cec_write_v1(NULL, &params_v1);
}

int host_cmd_cec_read(int port, struct ec_response_cec_read *response)
{
	struct ec_params_cec_read params = {
		.port = port,
	};

	return ec_cmd_cec_read(NULL, &params, response);
}

static int
host_cmd_get_next_event_v2(struct ec_response_get_next_event_v1 *response)
{
	struct host_cmd_handler_args args = BUILD_HOST_COMMAND_RESPONSE(
		EC_CMD_GET_NEXT_EVENT, 2, *response);

	return host_command_process(&args);
}

static int get_next_event_of_type(struct ec_response_get_next_event_v1 *event,
				  enum ec_mkbp_event event_type)
{
	/* Read MKBP events until we find one of the right type */
	while (host_cmd_get_next_event_v2(event) == EC_RES_SUCCESS) {
		if ((event->event_type & EC_MKBP_EVENT_TYPE_MASK) == event_type)
			return 0;
	}
	/* No more events */
	return -1;
}

int get_next_cec_mkbp_event(struct ec_response_get_next_event_v1 *event)
{
	return get_next_event_of_type(event, EC_MKBP_EVENT_CEC_EVENT);
}

bool cec_event_matches(struct ec_response_get_next_event_v1 *event, int port,
		       enum mkbp_cec_event events)
{
	return ((EC_MKBP_EVENT_CEC_GET_PORT(event->data.cec_events) == port) &&
		(EC_MKBP_EVENT_CEC_GET_EVENTS(event->data.cec_events) ==
		 events));
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

	zassert_ok(host_command_process(&args),
		   "Failed to get Type-C state for port %d", port);
}

void host_cmd_typec_control_enter_mode(int port, enum typec_mode mode)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_ENTER_MODE,
		.mode_to_enter = mode
	};

	zassert_ok(ec_cmd_typec_control(NULL, &params),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_exit_modes(int port)
{
	struct ec_params_typec_control params = {
		.port = port, .command = TYPEC_CONTROL_COMMAND_EXIT_MODES
	};

	zassert_ok(ec_cmd_typec_control(NULL, &params),
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

	zassert_ok(ec_cmd_typec_control(NULL, &params),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_clear_events(int port, uint32_t events)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_CLEAR_EVENTS,
		.clear_events_mask = events,
	};

	zassert_ok(ec_cmd_typec_control(NULL, &params),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_bist_share_mode(int port, int enable)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_BIST_SHARE_MODE,
		.bist_share_mode = enable
	};

	zassert_ok(ec_cmd_typec_control(NULL, &params),
		   "Failed to send Type-C control for port %d", port);
}

void host_cmd_typec_control_vdm_req(int port, struct typec_vdm_req vdm_req)
{
	struct ec_params_typec_control params = {
		.port = port,
		.command = TYPEC_CONTROL_COMMAND_SEND_VDM_REQ,
		.vdm_req_params = vdm_req,
	};

	zassert_ok(ec_cmd_typec_control(NULL, &params),
		   "Failed to send Type-C control for port %d", port);
}

struct ec_response_typec_vdm_response host_cmd_typec_vdm_response(int port)
{
	struct ec_params_typec_vdm_response params = { .port = port };
	struct ec_response_typec_vdm_response response;

	zassert_ok(ec_cmd_typec_vdm_response(NULL, &params, &response),
		   "Failed to get Type-C state for port %d", port);
	return response;
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

/* Implement the stub host_command_process function for tests with the upstream
 * Host Command to pass all needed parameters to the backend simulator.
 */
#ifdef CONFIG_EC_HOST_CMD
#define RX_HEADER_SIZE sizeof(struct ec_host_response)
#define TX_HEADER_SIZE sizeof(struct ec_host_request)
K_SEM_DEFINE(send_called, 0, 1);
static struct ec_host_cmd_tx_buf *tx_buf;

static int host_send(const struct ec_host_cmd_backend *backend)
{
	k_sem_give(&send_called);

	return 0;
}

static uint8_t cal_checksum(const uint8_t *const buffer, const uint16_t size)
{
	uint8_t checksum = 0;

	for (size_t i = 0; i < size; ++i) {
		checksum += buffer[i];
	}
	return (uint8_t)(-checksum);
}

static uint16_t pass_args_to_sim(struct host_cmd_handler_args *args)
{
	uint8_t rx_buf[args->params_size + RX_HEADER_SIZE];
	struct ec_host_request *rx_header = (struct ec_host_request *)rx_buf;
	struct ec_host_response *tx_header;
	int rv;

	k_sem_reset(&send_called);

	rx_header->struct_version = 3;
	rx_header->checksum = 0;
	rx_header->command = args->command;
	rx_header->command_version = args->version;
	rx_header->data_len = args->params_size;
	rx_header->reserved = 0;

	memcpy(rx_buf + RX_HEADER_SIZE, args->params, args->params_size);
	rx_header->checksum = cal_checksum(rx_buf, sizeof(rx_buf));

	ec_host_cmd_backend_sim_install_send_cb(host_send, &tx_buf);
	tx_buf->len_max = args->response_max + TX_HEADER_SIZE;

	/* Pass RX buffer to the backend simulator */
	ec_host_cmd_backend_sim_data_received(rx_buf, sizeof(rx_buf));

	/* Ensure send was called so we can verify outputs */
	rv = k_sem_take(&send_called, K_SECONDS(1));
	zassert_equal(rv, 0, "Send was not called");

	memcpy(args->response, (uint8_t *)tx_buf->buf + TX_HEADER_SIZE,
	       args->response_max);
	args->response_size = tx_buf->len - TX_HEADER_SIZE;
	tx_header = tx_buf->buf;

	return tx_header->result;
}

uint16_t host_command_process(struct host_cmd_handler_args *args)
{
	return pass_args_to_sim(args);
}

void host_command_received(struct host_cmd_handler_args *args)
{
	pass_args_to_sim(args);
}

#endif

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
