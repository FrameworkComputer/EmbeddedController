/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_tcpci.h"
#include "hooks.h"
#include "i2c.h"
#include "test/drivers/stubs.h"
#include "test/drivers/tcpci_test_common.h"

#include "tcpm/tcpci.h"
#include "test/drivers/test_state.h"

#define TCPCI_EMUL_NODE DT_NODELABEL(tcpci_emul)

/** Test TCPCI init and vbus level */
ZTEST(tcpci, test_generic_tcpci_init)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_init(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI release */
ZTEST(tcpci, test_generic_tcpci_release)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_release(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI get cc */
ZTEST(tcpci, test_generic_tcpci_get_cc)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_get_cc(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI set cc */
ZTEST(tcpci, test_generic_tcpci_set_cc)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_set_cc(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI set polarity */
ZTEST(tcpci, test_generic_tcpci_set_polarity)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_set_polarity(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI set vconn */
ZTEST(tcpci, test_generic_tcpci_set_vconn)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_set_vconn(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI set msg header */
ZTEST(tcpci, test_generic_tcpci_set_msg_header)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_set_msg_header(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI rx and sop prime enable */
ZTEST(tcpci, test_generic_tcpci_set_rx_detect)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_set_rx_detect(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI get raw message from TCPC revision 2.0 */
ZTEST(tcpci, test_generic_tcpci_get_rx_message_raw_rev2)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	/* Revision 2.0 is set by default in test_rules */
	test_tcpci_get_rx_message_raw(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI get raw message from TCPC revision 1.0 */
ZTEST(tcpci, test_generic_tcpci_get_rx_message_raw_rev1)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	test_tcpci_get_rx_message_raw(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI transmitting message from TCPC revision 2.0 */
ZTEST(tcpci, test_generic_tcpci_transmit_rev2)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	/* Revision 2.0 is set by default in test_rules */
	test_tcpci_transmit(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI transmitting message from TCPC revision 1.0 */
ZTEST(tcpci, test_generic_tcpci_transmit_rev1)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	test_tcpci_transmit(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI alert */
ZTEST(tcpci, test_generic_tcpci_alert)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_alert(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI alert RX message */
ZTEST(tcpci, test_generic_tcpci_alert_rx_message)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_alert_rx_message(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI auto discharge on disconnect */
ZTEST(tcpci, test_generic_tcpci_auto_discharge)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_auto_discharge(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI drp toggle */
ZTEST(tcpci, test_generic_tcpci_drp_toggle)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_drp_toggle(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI get chip info */
ZTEST(tcpci, test_generic_tcpci_get_chip_info)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_get_chip_info(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI enter low power mode */
ZTEST(tcpci, test_generic_tcpci_low_power_mode)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_low_power_mode(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI set bist test mode */
ZTEST(tcpci, test_generic_tcpci_set_bist_mode)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_set_bist_mode(emul, common_data, USBC_PORT_C0);
}

/** Test TCPCI discharge vbus */
ZTEST(tcpci, test_generic_tcpci_discharge_vbus)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	uint8_t exp_ctrl, initial_ctrl;

	/* Set initial value for POWER ctrl register. Chosen arbitrary. */
	initial_ctrl = TCPC_REG_POWER_CTRL_VBUS_VOL_MONITOR_DIS |
		       TCPC_REG_POWER_CTRL_VOLT_ALARM_DIS;
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_CTRL, initial_ctrl);

	/* Test discharge enable */
	exp_ctrl = initial_ctrl | TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_tcpc_discharge_vbus(USBC_PORT_C0, 1);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);

	/* Test discharge disable */
	exp_ctrl = initial_ctrl & ~TCPC_REG_POWER_CTRL_FORCE_DISCHARGE;
	tcpci_tcpc_discharge_vbus(USBC_PORT_C0, 0);
	check_tcpci_reg(emul, TCPC_REG_POWER_CTRL, exp_ctrl);
}

/** Test TCPC xfer */
ZTEST(tcpci, test_tcpc_xfer)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	uint16_t val, exp_val;
	uint8_t reg;

	/* Set value to register (value and register chosen arbitrary) */
	exp_val = 0x7fff;
	reg = TCPC_REG_ALERT_MASK;
	tcpci_emul_set_reg(emul, reg, exp_val);

	/* Test reading value using tcpc_xfer() function */
	zassert_equal(EC_SUCCESS,
		      tcpc_xfer(USBC_PORT_C0, &reg, 1, (uint8_t *)&val, 2),
		      NULL);
	zassert_equal(exp_val, val, "0x%x != 0x%x", exp_val, val);
}

/** Test TCPCI debug accessory enable/disable */
ZTEST(tcpci, test_generic_tcpci_debug_accessory)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	uint8_t exp_val, initial_val;

	/* Set initial value for STD output register. Chosen arbitrary. */
	initial_val = TCPC_REG_CONFIG_STD_OUTPUT_AUDIO_CONN_N |
		      TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB |
		      TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED |
		      TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_emul_set_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, initial_val);

	/* Test debug accessory connect */
	exp_val = initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_tcpc_debug_accessory(USBC_PORT_C0, 1);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);

	/* Test debug accessory disconnect */
	exp_val = initial_val | TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_tcpc_debug_accessory(USBC_PORT_C0, 0);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
}

/* Setup TCPCI usb mux to behave as it is used only for usb mux */
static void set_usb_mux_not_tcpc(void)
{
	usbc0_mux0.flags = USB_MUX_FLAG_NOT_TCPC;
}

/* Setup TCPCI usb mux to behave as it is used for usb mux and TCPC */
static void set_usb_mux_tcpc(void)
{
	usbc0_mux0.flags = 0;
}

/** Test TCPCI mux init */
ZTEST(tcpci, test_generic_tcpci_mux_init)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);
	const struct usb_mux *tcpci_usb_mux = usb_muxes[USBC_PORT_C0].mux;

	/* Set as usb mux with TCPC for first init call */
	set_usb_mux_tcpc();

	/* Make sure that TCPC is not accessed */
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);

	/* Set as only usb mux without TCPC for rest of the test */
	set_usb_mux_not_tcpc();

	/* Test fail on power status read */
	i2c_common_emul_set_read_fail_reg(common_data, TCPC_REG_POWER_STATUS);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on uninitialised bit set */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);

	/* Set default power status for rest of the test */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);

	/* Test fail on alert mask write fail */
	i2c_common_emul_set_write_fail_reg(common_data, TCPC_REG_ALERT_MASK);
	zassert_equal(EC_ERROR_UNKNOWN, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);

	/* Test fail on alert write fail */
	i2c_common_emul_set_write_fail_reg(common_data, TCPC_REG_ALERT);
	zassert_equal(EC_ERROR_UNKNOWN, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set arbitrary value to alert and alert mask registers */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0xffff);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT_MASK, 0xffff);

	/* Test success init */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0);
}

/** Test TCPCI mux enter low power mode */
ZTEST(tcpci, test_generic_tcpci_mux_enter_low_power)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);
	const struct usb_mux *tcpci_usb_mux = usb_muxes[USBC_PORT_C0].mux;

	/* Set as usb mux with TCPC for first enter_low_power call */
	set_usb_mux_tcpc();

	/* Make sure that TCPC is not accessed */
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);

	/* Set as only usb mux without TCPC for rest of the test */
	set_usb_mux_not_tcpc();

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(common_data, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux), NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct command is issued */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

/** Test TCPCI mux set and get */
static void test_generic_tcpci_mux_set_get(void)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);
	const struct usb_mux *tcpci_usb_mux = usb_muxes[USBC_PORT_C0].mux;
	mux_state_t mux_state, mux_state_get;
	uint16_t exp_val, initial_val;
	bool ack;

	mux_state = USB_PD_MUX_NONE;

	/* Test fail on standard output config register read */
	i2c_common_emul_set_read_fail_reg(common_data,
					  TCPC_REG_CONFIG_STD_OUTPUT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack), NULL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get), NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on standard output config register write */
	i2c_common_emul_set_write_fail_reg(common_data,
					   TCPC_REG_CONFIG_STD_OUTPUT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack), NULL);
	i2c_common_emul_set_write_fail_reg(common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set initial value for STD output register. Chosen arbitrary. */
	initial_val = TCPC_REG_CONFIG_STD_OUTPUT_AUDIO_CONN_N |
		      TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB |
		      TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED |
		      TCPC_REG_CONFIG_STD_OUTPUT_DBG_ACC_CONN_N;
	tcpci_emul_set_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, initial_val);

	/* Test setting/getting no MUX connection without polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_NONE;
	exp_val &= ~TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_NONE;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack), NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get), NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX DP with polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP |
		  TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack), NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get), NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX USB without polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	exp_val &= ~TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_USB_ENABLED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack), NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get), NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX USB and DP with polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB |
		  TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
		    USB_PD_MUX_POLARITY_INVERTED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack), NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get), NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);
}

ZTEST(tcpci, test_generic_tcpci_mux_set_get)
{
	test_generic_tcpci_mux_set_get();
}

ZTEST(tcpci, test_generic_tcpci_mux_set_get__not_tcpc)
{
	set_usb_mux_not_tcpc();
	test_generic_tcpci_mux_set_get();
	set_usb_mux_tcpc();
}

ZTEST(tcpci, test_generic_tcpci_hard_reset_reinit)
{
	const struct emul *emul = EMUL_DT_GET(TCPCI_EMUL_NODE);
	struct i2c_common_emul_data *common_data =
		emul_tcpci_generic_get_i2c_common_data(emul);

	test_tcpci_hard_reset_reinit(emul, common_data, USBC_PORT_C0);
}

void validate_mux_read_write16(const struct usb_mux *tcpci_usb_mux)
{
	const int reg = TCPC_REG_ALERT;
	const int expected = 65261;
	int restore = 0;
	int temp = 0;

	zassert_ok(mux_read16(tcpci_usb_mux, reg, &restore),
		   "Failed to read mux");

	if (IS_ENABLED(CONFIG_BUG_249829957)) {
		zassert_ok(mux_write16(tcpci_usb_mux, reg, expected),
			   "Failed to write mux");
		zassert_ok(mux_read16(tcpci_usb_mux, reg, &temp),
			   "Failed to read mux");
		zassert_equal(expected, temp, "expected=0x%X, read=0x%X",
			      expected, temp);
	}

	zassert_ok(mux_write16(tcpci_usb_mux, reg, restore),
		   "Failed to write mux");
}

/** Test usb_mux read/write APIs */
ZTEST(tcpci, test_usb_mux_read_write)
{
	struct usb_mux *tcpci_usb_mux = &usbc0_mux0;
	const int flags_restore = tcpci_usb_mux->flags;

	/* Configure mux read/writes for TCPC APIs */
	tcpci_usb_mux->flags &= ~USB_MUX_FLAG_NOT_TCPC;
	validate_mux_read_write16(tcpci_usb_mux);

	/* Configure mux read/writes for I2C APIs */
	tcpci_usb_mux->flags |= USB_MUX_FLAG_NOT_TCPC;
	validate_mux_read_write16(tcpci_usb_mux);

	tcpci_usb_mux->flags = flags_restore;
}

static void *tcpci_setup(void)
{
	/* This test suite assumes that first usb mux for port C0 is TCPCI */
	__ASSERT(usb_muxes[USBC_PORT_C0].mux->driver ==
			 &tcpci_tcpm_usb_mux_driver,
		 "Invalid config of usb_muxes in test/drivers/src/stubs.c");

	return NULL;
}

static void tcpci_after(void *state)
{
	set_usb_mux_tcpc();
}

ZTEST_SUITE(tcpci, drivers_predicate_pre_main, tcpci_setup, NULL, tcpci_after,
	    NULL);
