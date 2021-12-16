/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_tcpci.h"
#include "hooks.h"
#include "i2c.h"
#include "stubs.h"
#include "tcpci_test_common.h"

#include "tcpm/tcpci.h"

#define EMUL_LABEL DT_NODELABEL(tcpci_emul)

/** Test TCPCI init and vbus level */
static void test_generic_tcpci_init(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_init(emul, USBC_PORT_C0);
}

/** Test TCPCI release */
static void test_generic_tcpci_release(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_release(emul, USBC_PORT_C0);
}

/** Test TCPCI get cc */
static void test_generic_tcpci_get_cc(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_get_cc(emul, USBC_PORT_C0);
}

/** Test TCPCI set cc */
static void test_generic_tcpci_set_cc(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_set_cc(emul, USBC_PORT_C0);
}

/** Test TCPCI set polarity */
static void test_generic_tcpci_set_polarity(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_set_polarity(emul, USBC_PORT_C0);
}

/** Test TCPCI set vconn */
static void test_generic_tcpci_set_vconn(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_set_vconn(emul, USBC_PORT_C0);
}

/** Test TCPCI set msg header */
static void test_generic_tcpci_set_msg_header(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_set_msg_header(emul, USBC_PORT_C0);
}

/** Test TCPCI rx and sop prime enable */
static void test_generic_tcpci_set_rx_detect(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_set_rx_detect(emul, USBC_PORT_C0);
}

/** Test TCPCI get raw message from TCPC revision 2.0 */
static void test_generic_tcpci_get_rx_message_raw_rev2(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	test_tcpci_get_rx_message_raw(emul, USBC_PORT_C0);
}

/** Test TCPCI get raw message from TCPC revision 1.0 */
static void test_generic_tcpci_get_rx_message_raw_rev1(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	test_tcpci_get_rx_message_raw(emul, USBC_PORT_C0);
}

/** Test TCPCI transmitting message from TCPC revision 2.0 */
static void test_generic_tcpci_transmit_rev2(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = TCPC_FLAGS_TCPCI_REV2_0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV2_0_VER1_1);

	test_tcpci_transmit(emul, USBC_PORT_C0);
}

/** Test TCPCI transmitting message from TCPC revision 1.0 */
static void test_generic_tcpci_transmit_rev1(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	tcpc_config[USBC_PORT_C0].flags = 0;
	tcpci_emul_set_rev(emul, TCPCI_EMUL_REV1_0_VER1_0);

	test_tcpci_transmit(emul, USBC_PORT_C0);
}

/** Test TCPCI alert */
static void test_generic_tcpci_alert(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_alert(emul, USBC_PORT_C0);
}


/** Test TCPCI alert RX message */
static void test_generic_tcpci_alert_rx_message(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_alert_rx_message(emul, USBC_PORT_C0);
}

/** Test TCPCI auto discharge on disconnect */
static void test_generic_tcpci_auto_discharge(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_auto_discharge(emul, USBC_PORT_C0);
}

/** Test TCPCI drp toggle */
static void test_generic_tcpci_drp_toggle(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_drp_toggle(emul, USBC_PORT_C0);
}

/** Test TCPCI get chip info */
static void test_generic_tcpci_get_chip_info(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_get_chip_info(emul, USBC_PORT_C0);
}

/** Test TCPCI enter low power mode */
static void test_generic_tcpci_low_power_mode(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_low_power_mode(emul, USBC_PORT_C0);
}

/** Test TCPCI set bist test mode */
static void test_generic_tcpci_set_bist_mode(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));

	test_tcpci_set_bist_mode(emul, USBC_PORT_C0);
}

/** Test TCPCI discharge vbus */
void test_generic_tcpci_discharge_vbus(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
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
static void test_tcpc_xfer(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
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
static void test_generic_tcpci_debug_accessory(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
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
	usb_muxes[USBC_PORT_C0].flags = USB_MUX_FLAG_NOT_TCPC;
}

/* Setup TCPCI usb mux to behave as it is used for usb mux and TCPC */
static void set_usb_mux_tcpc(void)
{
	usb_muxes[USBC_PORT_C0].flags = 0;
}

/** Test TCPCI mux init */
static void test_generic_tcpci_mux_init(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct usb_mux *tcpci_usb_mux = &usb_muxes[USBC_PORT_C0];

	/* Set as usb mux with TCPC for first init call */
	set_usb_mux_tcpc();

	/* Make sure that TCPC is not accessed */
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);

	/* Set as only usb mux without TCPC for rest of the test */
	set_usb_mux_not_tcpc();

	/* Test fail on power status read */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_POWER_STATUS);
	zassert_equal(EC_ERROR_INVAL, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on uninitialised bit set */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_UNINIT);
	zassert_equal(EC_ERROR_TIMEOUT,
		      tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);

	/* Set default power status for rest of the test */
	tcpci_emul_set_reg(emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);

	/* Test fail on alert mask write fail */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ALERT_MASK);
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);

	/* Test fail on alert write fail */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_ALERT);
	zassert_equal(EC_ERROR_UNKNOWN,
		      tcpci_tcpm_mux_init(tcpci_usb_mux), NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Set arbitrary value to alert and alert mask registers */
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT, 0xffff);
	tcpci_emul_set_reg(emul, TCPC_REG_ALERT_MASK, 0xffff);

	/* Test success init */
	zassert_equal(EC_SUCCESS, tcpci_tcpm_mux_init(tcpci_usb_mux),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_ALERT_MASK, 0);
	check_tcpci_reg(emul, TCPC_REG_ALERT, 0);
}

/** Test TCPCI mux enter low power mode */
static void test_generic_tcpci_mux_enter_low_power(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct usb_mux *tcpci_usb_mux = &usb_muxes[USBC_PORT_C0];

	/* Set as usb mux with TCPC for first enter_low_power call */
	set_usb_mux_tcpc();

	/* Make sure that TCPC is not accessed */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_FAIL_ALL_REG);
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);

	/* Set as only usb mux without TCPC for rest of the test */
	set_usb_mux_not_tcpc();

	/* Test error on failed command set */
	i2c_common_emul_set_write_fail_reg(i2c_emul, TCPC_REG_COMMAND);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test correct command is issued */
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_enter_low_power(tcpci_usb_mux),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_I2CIDLE);
}

/** Test TCPCI mux set and get */
static void test_generic_tcpci_mux_set_get(void)
{
	const struct emul *emul = emul_get_binding(DT_LABEL(EMUL_LABEL));
	struct i2c_emul *i2c_emul = tcpci_emul_get_i2c_emul(emul);
	struct usb_mux *tcpci_usb_mux = &usb_muxes[USBC_PORT_C0];
	mux_state_t mux_state, mux_state_get;
	uint16_t exp_val, initial_val;
	bool ack;

	mux_state = USB_PD_MUX_NONE;

	/* Test fail on standard output config register read */
	i2c_common_emul_set_read_fail_reg(i2c_emul, TCPC_REG_CONFIG_STD_OUTPUT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Test fail on standard output config register write */
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   TCPC_REG_CONFIG_STD_OUTPUT);
	zassert_equal(EC_ERROR_INVAL,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
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
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX DP with polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_DP |
		  TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_DP_ENABLED | USB_PD_MUX_POLARITY_INVERTED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);

	/* Test setting/getting MUX USB without polarity inverted */
	exp_val = (initial_val & ~TCPC_REG_CONFIG_STD_OUTPUT_MUX_MASK) |
		  TCPC_REG_CONFIG_STD_OUTPUT_MUX_USB;
	exp_val &= ~TCPC_REG_CONFIG_STD_OUTPUT_CONNECTOR_FLIPPED;
	mux_state = USB_PD_MUX_USB_ENABLED;
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
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
		      tcpci_tcpm_mux_set(tcpci_usb_mux, mux_state, &ack),
		      NULL);
	check_tcpci_reg(emul, TCPC_REG_CONFIG_STD_OUTPUT, exp_val);
	zassert_false(ack, "Ack from host shouldn't be required");
	zassert_equal(EC_SUCCESS,
		      tcpci_tcpm_mux_get(tcpci_usb_mux, &mux_state_get),
		      NULL);
	zassert_equal(mux_state, mux_state_get, "Expected state 0x%x, got 0x%x",
		      mux_state, mux_state_get);
}

void test_suite_tcpci(void)
{
	/* This test suite assumes that first usb mux for port C0 is TCPCI */
	__ASSERT(usb_muxes[USBC_PORT_C0].driver == &tcpci_tcpm_usb_mux_driver,
		 "Invalid config of usb_muxes in test/drivers/src/stubs.c");

	ztest_test_suite(tcpci,
			 ztest_unit_test(test_generic_tcpci_init),
			 ztest_unit_test(test_generic_tcpci_release),
			 ztest_unit_test(test_generic_tcpci_get_cc),
			 ztest_unit_test(test_generic_tcpci_set_cc),
			 ztest_unit_test(test_generic_tcpci_set_polarity),
			 ztest_unit_test(test_generic_tcpci_set_vconn),
			 ztest_unit_test(test_generic_tcpci_set_msg_header),
			 ztest_unit_test(test_generic_tcpci_set_rx_detect),
			 ztest_unit_test(
				test_generic_tcpci_get_rx_message_raw_rev2),
			 ztest_unit_test(test_generic_tcpci_transmit_rev2),
			 ztest_unit_test(
				test_generic_tcpci_get_rx_message_raw_rev1),
			 ztest_unit_test(test_generic_tcpci_transmit_rev1),
			 ztest_unit_test(test_generic_tcpci_alert),
			 ztest_unit_test(test_generic_tcpci_alert_rx_message),
			 ztest_unit_test(test_generic_tcpci_auto_discharge),
			 ztest_unit_test(test_generic_tcpci_drp_toggle),
			 ztest_unit_test(test_generic_tcpci_get_chip_info),
			 ztest_unit_test(test_generic_tcpci_low_power_mode),
			 ztest_unit_test(test_generic_tcpci_set_bist_mode),
			 ztest_unit_test(test_generic_tcpci_discharge_vbus),
			 ztest_unit_test(test_tcpc_xfer),
			 ztest_unit_test(test_generic_tcpci_debug_accessory),
			 ztest_unit_test(test_generic_tcpci_mux_init),
			 ztest_unit_test(
				test_generic_tcpci_mux_enter_low_power),
			 /* Test set/get with usb mux and without TCPC */
			 ztest_unit_test_setup_teardown(
				test_generic_tcpci_mux_set_get,
				set_usb_mux_not_tcpc, set_usb_mux_tcpc),
			 /* Test set/get with usb mux and TCPC */
			 ztest_unit_test(test_generic_tcpci_mux_set_get));
	ztest_run_test_suite(tcpci);
}
