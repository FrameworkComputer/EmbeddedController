/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_INCLUDE_UTILS_H_
#define ZEPHYR_TEST_DRIVERS_INCLUDE_UTILS_H_

#include "charger.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "extpower.h"
#include "host_command.h"
#include "lpc.h"
#include "power.h"
#include "usbc/utils.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

/**
 * @brief Helper macro to check for the NTC38xx TCPC. The NCT38xx TCPC
 * is configured as a child binding under the nuvoton,nc38xx MFD. Grab
 * the parent phanlde when the NCT38xx TCPC is detected, otherwise return
 * the current node phandle.
 */
#define EMUL_GET_CHIP_BINDING(chip_phandle)                                 \
	COND_CODE_1(DT_NODE_HAS_COMPAT(chip_phandle, nuvoton_nct38xx_tcpc), \
		    (EMUL_DT_GET(DT_PARENT(chip_phandle))),                 \
		    (EMUL_DT_GET(chip_phandle)))

/**
 * @brief Helper macro for EMUL_GET_USBC_BINDING. If @p usbc_id has the same
 *        port number as @p port, then struct emul* for @p chip phandle is
 *        returned.
 *
 * @param usbc_id Named usbc port ID
 * @param port Port number to match with named usbc port
 * @param chip Name of chip phandle property
 */
#define EMUL_GET_USBC_BINDING_IF_PORT_MATCH(usbc_id, port, chip) \
	COND_CODE_1(IS_EQ(USBC_PORT_NEW(usbc_id), port),         \
		    (EMUL_GET_CHIP_BINDING(DT_PHANDLE(usbc_id, chip))), ())

/**
 * @brief Get struct emul from phandle @p chip property of USBC @p port
 *
 * @param port Named usbc port number. The value has to be integer literal.
 * @param chip Name of chip property that is phandle to required emulator.
 */
#define EMUL_GET_USBC_BINDING(port, chip)                                 \
	DT_FOREACH_STATUS_OKAY_VARGS(named_usbc_port,                     \
				     EMUL_GET_USBC_BINDING_IF_PORT_MATCH, \
				     port, chip)

/** @brief Set emulated battery level. Call all necessary hooks. */
void test_set_battery_level(int percentage);

/** @brief Set chipset to S0 state. Call all necessary hooks. */
void test_set_chipset_to_s0(void);

/**
 * @brief Set the chipset to any stable state. Call all necessary hooks.
 *
 * Supported states are:
 * <ul>
 *   <li>POWER_G3 (same as calling test_set_chipset_to_g3())</li>
 *   <li>POWER_S5</li>
 *   <li>POWER_S4</li>
 *   <li>POWER_S3</li>
 *   <li>POWER_S0 (same as calling test_set_chipset_to_s0()</li>
 *   <li>POWER_S0ix (if either CONFIG_PLATFORM_EC_POWERSEQ_S0IX or
 *       CONFIG_AP_PWRSEQ_S0IX are enabled)</li>
 * </ul>
 *
 * @param new_state The new state. Must be a steady state (see above).
 */
void test_set_chipset_to_power_level(enum power_state new_state);

/** @brief Set chipset to G3 state. Call all necessary hooks. */
void test_set_chipset_to_g3(void);

/**
 * Run an ACPI read to the specified address.
 *
 * This function assumes a successful ACPI read process and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param acpi_addr Address to query
 * @return Byte read
 */
uint8_t acpi_read(uint8_t acpi_addr);

/**
 * Run an ACPI write to the specified address.
 *
 * This function assumes a successful ACPI write process and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param acpi_addr Address to write
 * @param write_byte Byte to write to address
 */
void acpi_write(uint8_t acpi_addr, uint8_t write_byte);

/**
 * Run the host command to gather our EC feature flags.
 *
 * This function assumes a successful host command processing and will make a
 * call to the zassert_* API.  A failure here will fail the calling test.
 *
 * @return The result of the host command
 */
static inline struct ec_response_get_features host_cmd_get_features(void)
{
	struct ec_response_get_features response;

	zassert_ok(ec_cmd_get_features(NULL, &response),
		   "Failed to get features");
	return response;
}

/**
 * Run the host command to get the charge state for a given charger number.
 *
 * This function assumes a successful host command processing and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param chgnum The charger number to query.
 * @return The result of the query.
 */
static inline struct ec_response_charge_state host_cmd_charge_state(int chgnum)
{
	struct ec_params_charge_state params = {
		.chgnum = chgnum,
		.cmd = CHARGE_STATE_CMD_GET_STATE,
	};
	struct ec_response_charge_state response;

	zassert_ok(ec_cmd_charge_state(NULL, &params, &response),
		   "Failed to get charge state for chgnum %d", chgnum);
	return response;
}

/**
 * Run the host command to get the USB PD power info for a given port.
 *
 * This function assumes a successful host command processing and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param port The USB port to get info from.
 * @return The result of the query.
 */
static inline struct ec_response_usb_pd_power_info host_cmd_power_info(int port)
{
	struct ec_params_usb_pd_power_info params = { .port = port };
	struct ec_response_usb_pd_power_info response;

	zassert_ok(ec_cmd_usb_pd_power_info(NULL, &params, &response),
		   "Failed to get power info for port %d", port);
	return response;
}

/**
 * Run the host command to get the Type-C status information for a given port.
 *
 * This function assumes a successful host command processing and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param port The USB port to get info from.
 * @return The result of the query.
 */
static inline struct ec_response_typec_status host_cmd_typec_status(int port)
{
	struct ec_params_typec_status params = { .port = port };
	struct ec_response_typec_status response;

	zassert_ok(ec_cmd_typec_status(NULL, &params, &response),
		   "Failed to get Type-C state for port %d", port);
	return response;
}

/**
 * Run the host command to get the most recent VDM response for the AP
 *
 * This function asserts a successful host command processing and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param port The USB port to get info from.
 * @return The result of the query.
 */
struct ec_response_typec_vdm_response host_cmd_typec_vdm_response(int port);

static inline struct ec_response_usb_pd_control
host_cmd_usb_pd_control(int port, enum usb_pd_control_swap swap)
{
	struct ec_params_usb_pd_control params = { .port = port, .swap = swap };
	struct ec_response_usb_pd_control response;

	zassert_ok(ec_cmd_usb_pd_control(NULL, &params, &response),
		   "Failed to process usb_pd_control_swap for port %d, swap %d",
		   port, swap);
	return response;
}

/**
 * Run the host command to suspend/resume PD ports
 *
 * This function assumes a successful host command processing and will make a
 * call to the zassert_* API. A failure here will fail the calling test.
 *
 * @param port The USB port to operate on
 * @param cmd The sub-command to run
 */
static inline void host_cmd_pd_control(int port, enum ec_pd_control_cmd cmd)
{
	struct ec_params_pd_control params = { .chip = port, .subcmd = cmd };

	zassert_ok(ec_cmd_pd_control(NULL, &params),
		   "Failed to process pd_control for port %d, cmd %d", port,
		   cmd);
}

/**
 * Run the host command to control or query the charge state
 *
 * @return The result of the query.
 */
static inline struct ec_response_charge_control
host_cmd_charge_control(enum ec_charge_control_mode mode,
			enum ec_charge_control_cmd cmd)
{
	struct ec_params_charge_control params = { .cmd = cmd,
						   .mode = mode,
						   .sustain_soc = {
							   .lower = -1,
							   .upper = -1,
						   } };
	struct ec_response_charge_control response;

	zassert_ok(ec_cmd_charge_control_v2(NULL, &params, &response),
		   "Failed to get charge control values");

	return response;
}

/**
 * @brief Call the host command HOST_EVENT with the user supplied action.
 *
 * @param action    - HOST_EVENT action parameter.
 * @param mask_type - Event mask type to apply to the HOST_EVENT action.
 * @param r         - Pointer to the response object to fill.
 */
enum ec_status host_cmd_host_event(enum ec_host_event_action action,
				   enum ec_host_event_mask_type mask_type,
				   struct ec_response_host_event *r);

/**
 * @brief Call the host command MOTION_SENSE with the dump sub-command
 *
 * Note: this function uses the zassert_ API. It will fail the test if the host
 * command fails.
 *
 * @param max_sensor_count The maximum number of sensor data objects to populate
 *        in the response object.
 * @param response Pointer to the response object to fill.
 * @param response_size Size of the response buffer.
 */
void host_cmd_motion_sense_dump(int max_sensor_count,
				struct ec_response_motion_sense *response,
				size_t response_size);

/**
 * @brief Call the host command MOTION_SENSE with the data sub-command
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_data(uint8_t sensor_num,
			       struct ec_response_motion_sense *response);

/**
 * @brief Call the host command MOTION_SENSE with the info sub-command
 *
 * @param cmd_version The command version
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_info(uint8_t cmd_version, uint8_t sensor_num,
			       struct ec_response_motion_sense *response);

/**
 * @brief Call the host command MOTION_SENSE with the ec_rate sub-command
 *
 * This function performs a read of the current rate by passing
 * EC_MOTION_SENSE_NO_VALUE as the data rate. Otherwise, the data rate should be
 * updated.
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param data_rate_ms The new data rate or EC_MOTION_SENSE_NO_VALUE to read
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_ec_rate(uint8_t sensor_num, int data_rate_ms,
				  struct ec_response_motion_sense *response);

/**
 * @brief Call the host command MOTION_SENSE with the odr sub-command
 *
 * This function performs a read of the current odr by passing
 * EC_MOTION_SENSE_NO_VALUE as the data rate. Otherwise, the data rate should be
 * updated.
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param odr The new ODR to set
 * @param round_up Whether or not to round up the ODR
 * @param response Pointer to the response data structure to fill on success
 * @return The result code form the host command
 */
int host_cmd_motion_sense_odr(uint8_t sensor_num, int32_t odr, bool round_up,
			      struct ec_response_motion_sense *response);

/**
 * @brief Call the host command MOTION_SENSE with the sensor range sub-command
 *
 * This function attempts to set the sensor range and returns the range value.
 * If the range value is EC_MOTION_SENSE_NO_VALUE, then the host command will
 * not attempt to update the range.
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param range The new range to set
 * @param round_up Whether or not to round up the range.
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_range(uint8_t sensor_num, int32_t range,
				bool round_up,
				struct ec_response_motion_sense *response);

/**
 * @brief Call the host command MOTION_SENSE with the sensor offset sub-command
 *
 * This function attempts to set the offset if the flags field includes
 * MOTION_SENSE_SET_OFFSET. Otherwise, the temperature and offsets are ignored.
 * The response field will include the current (after modification) offsets and
 * temperature.
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param flags The flags to pass to the host command
 * @param temperature The temperature at which the offsets were attained (set)
 * @param offset_x The X offset to set
 * @param offset_y The Y offset to set
 * @param offset_z The Z offset to set
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_offset(uint8_t sensor_num, uint16_t flags,
				 int16_t temperature, int16_t offset_x,
				 int16_t offset_y, int16_t offset_z,
				 struct ec_response_motion_sense *response);

/**
 * @brief Call the host command MOTION_SENSE with the sensor scale sub-command
 *
 * This function attempts to set the scale if the flags field includes
 * MOTION_SENSE_SET_OFFSET. Otherwise, the temperature and scales are ignored.
 * The response field will include the current (after modification) scales and
 * temperature.
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param flags The flags to pass to the host command
 * @param temperature The temperature at which the scales were attained (set)
 * @param scale_x The X scale to set
 * @param scale_y The Y scale to set
 * @param scale_z The Z scale to set
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_scale(uint8_t sensor_num, uint16_t flags,
				int16_t temperature, int16_t scale_x,
				int16_t scale_y, int16_t scale_z,
				struct ec_response_motion_sense *response);

/**
 * @brief Enable/disable sensor calibration via host command
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param enable Whether to enable or disable the calibration
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_calib(uint8_t sensor_num, bool enable,
				struct ec_response_motion_sense *response);

/**
 * @brief Set the sensor's fifo flush bit
 *
 * @param sensor_num The sensor index in the motion_sensors array to query
 * @param response Pointer to the response data structure to fill on success
 * @param response_size Size of the response buffer.
 * @return The result code from the host command
 */
int host_cmd_motion_sense_fifo_flush(uint8_t sensor_num,
				     struct ec_response_motion_sense *response,
				     size_t response_size);

/**
 * @brief Get the current fifo info
 *
 * @param response Pointer to the response data structure to fill on success
 * @param response_size Size of the response buffer.
 * @return The result code from the host command
 */
int host_cmd_motion_sense_fifo_info(struct ec_response_motion_sense *response,
				    size_t response_size);

/**
 * @brief Get the current fifo data
 *
 * @param buffer_length The number of entries available on the response pointer
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_fifo_read(uint8_t buffer_length,
				    struct ec_response_motion_sense *response);

/**
 * @brief Call the int_enable motionsense host command
 *
 * @param enable 0 for disable, 1 for enable. All others are invalid
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_int_enable(int8_t enable,
				     struct ec_response_motion_sense *response);

/**
 * @brief Call the spoof motion_sense subcommand
 *
 * @param sensor_num The sensor index in motion_sensors
 * @param enable The enable field, for normal operations this will be one of
 * enum motionsense_spoof_mode
 * @param values0 The X value to set if using custom mode
 * @param values1 The Y value to set if using custom mode
 * @param values2 The Z value to set if using custom mode
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_spoof(uint8_t sensor_num, uint8_t enable,
				int16_t values0, int16_t values1,
				int16_t values2,
				struct ec_response_motion_sense *response);

/**
 * @brief Call the keyboard wake angle motion_sense subcommand
 *
 * @param data Angle to set
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_kb_wake_angle(
	int16_t data, struct ec_response_motion_sense *response);

/**
 * @brief Call the lid angle motion_sense subcommand
 *
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_lid_angle(struct ec_response_motion_sense *response);

/**
 * @brief Call the tablet mode lid angle threshold motion_sense subcommand
 *
 * @param lid_angle Lid angle for transitioning to tablet mode
 * @param hys_degree Hysteresis or above transition
 * @param response Pointer to the response data structure to fill on success
 * @return The result code from the host command
 */
int host_cmd_motion_sense_tablet_mode_lid_angle(
	int16_t lid_angle, int16_t hys_degree,
	struct ec_response_motion_sense *response);

/**
 * Run host command to set CEC parameters.
 *
 * @param port	  CEC port number
 * @param cmd	  Parameter to set
 * @param val	  Value to set the parameter to
 * @return	  Return value from the host command
 */
int host_cmd_cec_set(int port, enum cec_command cmd, uint8_t val);

/**
 * Run host command to get CEC parameters.
 *
 * @param port	    CEC port number
 * @param cmd	    Parameter to get
 * @param response  Response struct containing parameter value
 * @return	    Return value from the host command
 */
int host_cmd_cec_get(int port, enum cec_command cmd,
		     struct ec_response_cec_get *response);

/**
 * Run v0 host command to write a CEC message.
 * Note, v0 always operates on port 0.
 *
 * @param msg	    Buffer containing the message
 * @param msg_len   Message length in bytes
 * @return	    Return value from the host command
 */
int host_cmd_cec_write(const uint8_t *msg, uint8_t msg_len);

/**
 * Run v1 host command to write a CEC message.
 *
 * @param port	    CEC port number
 * @param msg	    Buffer containing the message
 * @param msg_len   Message length in bytes
 * @return	    Return value from the host command
 */
int host_cmd_cec_write_v1(int port, const uint8_t *msg, uint8_t msg_len);

/**
 * Run host command to read a CEC message.
 *
 * @param port	    CEC port number
 * @param response  Response struct containing the message read
 * @return	    Return value from the host command
 */
int host_cmd_cec_read(int port, struct ec_response_cec_read *response);

/**
 * Read MKBP events until we find one of type EC_MKBP_EVENT_CEC_EVENT.
 *
 * @param event	    The MKBP event on success
 * @return	    0 if an event was found, -1 otherwise
 */
int get_next_cec_mkbp_event(struct ec_response_get_next_event_v1 *event);

/**
 * Check if the given MKBP event matches the given port and event type.
 *
 * @param event	    An MKBP event of type EC_MKBP_EVENT_CEC_EVENT.
 * @param port	    Port to match against
 * @param events    Event type to match against
 * @return	    true if the event matches, false otherwise
 */
bool cec_event_matches(struct ec_response_get_next_event_v1 *event, int port,
		       enum mkbp_cec_event events);

/**
 * Run the host command to get the PD discovery responses.
 *
 * @param port          The USB-C port number
 * @param partner_type  SOP, SOP', or SOP''
 * @param response      Destination buffer for command response;
 *                      should hold struct ec_response_typec_discovery and
 *                      enough struct svid_mode_info for expected response.
 * @param response_size Number of bytes in response
 */
void host_cmd_typec_discovery(int port, enum typec_partner_type partner_type,
			      void *response, size_t response_size);

/**
 * Run the host command to control PD port behavior, with the sub-command of
 * TYPEC_CONTROL_COMMAND_ENTER_MODE
 *
 * @param port	The USB-C port number
 * @param mode	Mode to enter
 */
void host_cmd_typec_control_enter_mode(int port, enum typec_mode mode);

/**
 * Run the host command to control PD port behavior, with the sub-command of
 * TYPEC_CONTROL_COMMAND_EXIT_MODES
 *
 * @param port      The USB-C port number
 */
void host_cmd_typec_control_exit_modes(int port);

/**
 * Run the host command to control PD port behavior, with the sub-command of
 * TYPEC_CONTROL_COMMAND_USB_MUX_SET
 *
 * @param port		The USB-C port number
 * @param mux_set	Mode and mux index to set
 */
void host_cmd_typec_control_usb_mux_set(int port,
					struct typec_usb_mux_set mux_set);

/**
 * Run the host command to control PD port behavior, with the sub-command of
 * TYPEC_CONTROL_COMMAND_CLEAR_EVENTS
 *
 * @param port		The USB-C port number
 * @param events	Events to clear for the port (see PD_STATUS_EVENT_*
 *			definitions for options)
 */
void host_cmd_typec_control_clear_events(int port, uint32_t events);

/**
 * Run the host command to control PD port behavior, with the sub-command of
 * TYPEC_CONTROL_COMMAND_BIST_SHARE_MODE
 *
 * @param port		The USB-C port number
 * @param enable	enable bist share mode or not
 */
void host_cmd_typec_control_bist_share_mode(int port, int enable);

/**
 * Run the host command to control PD port behavior, with the sub-command of
 * TYPEC_CONTROL_COMMAND_SEND_VDM_REQ
 *
 * @param port		The USB-C port number
 * @param vdm_req	VDM request data
 */
void host_cmd_typec_control_vdm_req(int port, struct typec_vdm_req vdm_req);

struct host_events_ctx {
	host_event_t lpc_host_events;
	host_event_t lpc_host_event_mask[LPC_HOST_EVENT_COUNT];
};

/**
 * Save all host events. This should be run as part of the "before" action for
 * any test suite that manipulates the host events.
 *
 * @param host_events_ctx	Caller allocated storage to save the host
 *				events.
 */
void host_events_save(struct host_events_ctx *host_events_ctx);

/**
 * Restore all host events. This should be run as part of the "after" action for
 * any test suite that manipulates the host events.
 *
 * @param host_events_ctx	Saved host events context information.
 */
void host_events_restore(struct host_events_ctx *host_events_ctx);

#define GPIO_ACOK_OD_NODE DT_NODELABEL(gpio_acok_od)
#define GPIO_ACOK_OD_PIN DT_GPIO_PIN(GPIO_ACOK_OD_NODE, gpios)

/**
 * Set whether or not AC is enabled.
 *
 * If enabled, the device _should_ begin charging.
 *
 * This function assumes a successful gpio emulator call and will make a call
 * to the zassert_* API. A failure here will fail the calling test.
 *
 * This function sleeps to wait for the GPIO interrupt to take place.
 *
 * @param enabled Whether or not to enable AC.
 */
static inline void set_ac_enabled(bool enabled)
{
	const struct device *acok_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_ACOK_OD_NODE, gpios));

	zassert_ok(gpio_emul_input_set(acok_dev, GPIO_ACOK_OD_PIN, enabled),
		   NULL);
	/*
	 * b/253284635 - Sleep for a full second past the debounce time
	 * to ensure the power button debounce logic runs.
	 */
	k_sleep(K_MSEC(CONFIG_EXTPOWER_DEBOUNCE_MS + 1000));
	zassert_equal(enabled, extpower_is_present(), NULL);
}

/**
 * @brief Connect a power source to a given port.
 *
 * Note: this is function currently only supports an ISL923X charger chip.
 *
 * @param partner Pointer to the emulated TCPCI partner device
 * @param src Pointer to the emulated source extension
 * @param pdo_index The index of the PDO object within the src to use
 * @param tcpci_emul The TCPCI emulator that the source will connect to
 * @param charger_emul The charger chip emulator
 */
void connect_source_to_port(struct tcpci_partner_data *partner,
			    struct tcpci_src_emul_data *src, int pdo_index,
			    const struct emul *tcpci_emul,
			    const struct emul *charger_emul);

/**
 * @brief Disconnect a power source from a given port.
 *
 * Note: this is function currently only supports an ISL923X charger chip.
 *
 * @param tcpci_emul The TCPCI emulator that will be disconnected
 * @param charger_emul The charger chip emulator
 */
void disconnect_source_from_port(const struct emul *tcpci_emul,
				 const struct emul *charger_emul);

/**
 * @brief Connect a power sink to a given port.
 *
 * Note: this is function currently only supports an ISL923X charger chip.
 *
 * @param partner Pointer to the emulated TCPCI partner device
 * @param tcpci_emul The TCPCI emulator that the source will connect to
 * @param charger_emul The charger chip emulator
 */
void connect_sink_to_port(struct tcpci_partner_data *partner,
			  const struct emul *tcpci_emul,
			  const struct emul *charger_emul);

/**
 * @brief Disconnect a power sink from a given port.
 *
 * @param tcpci_emul The TCPCI emulator that will be disconnected
 */
void disconnect_sink_from_port(const struct emul *tcpci_emul);

/**
 * @brief Allocate memory for a test pourpose
 *
 * @param bytes Number of bytes to allocate
 *
 * @return Pointer to valid memory or NULL
 */
void *test_malloc(size_t bytes);

/**
 * @brief Free memory allocated by @ref test_malloc
 *
 * @param mem Pointer to the memory
 */
void test_free(void *mem);

/**
 * @brief Force the chipset to state G3 and then transition to S3 and finally
 * S5.
 *
 */
void test_set_chipset_to_g3_then_transition_to_s5(void);

/**
 * @brief Checks console command with expected console output and expected
 * return value
 *
 */
#define CHECK_CONSOLE_CMD(cmd, expected_output, expected_rv)                 \
	check_console_cmd((cmd), (expected_output), (expected_rv), __FILE__, \
			  __LINE__)
void check_console_cmd(const char *cmd, const char *expected_output,
		       const int expected_rv, const char *file, const int line);

/* The upstream struct ec_host_cmd_handler_args omits the result field, so skip
 * checks of the result when using the upstream host commands.
 */
#define CHECK_ARGS_RESULT(args) \
	COND_CODE_0(CONFIG_EC_HOST_CMD, (zassert_ok(args.result, NULL);), ())
#endif /* ZEPHYR_TEST_DRIVERS_INCLUDE_UTILS_H_ */
