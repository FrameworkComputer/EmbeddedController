/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "cbi_ssfc.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "cros_board_info.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/retimer/nb7v904m.h"
#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/pi3usb3x532.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_8042.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "stdbool.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

/* C0 interrupt line shared by BC 1.2 and charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	/*
	 * The interrupt line is shared between the TCPC and BC 1.2 detection
	 * chip.  Therefore we'll need to check both ICs.
	 */
	schedule_deferred_pd_interrupt(0);
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void check_c0_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		notify_c0_chips();
		hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
	}
}

static void usb_c0_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c0_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c0_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
}

/* C1 interrupt line shared by BC 1.2, TCPC, and charger */
static void check_c1_line(void);
DECLARE_DEFERRED(check_c1_line);

static void notify_c1_chips(void)
{
	schedule_deferred_pd_interrupt(1);
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

static void check_c1_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips.
	 */
	if (!gpio_get_level(GPIO_SUB_C1_INT_EN_RAILS_ODL)) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

static void sub_usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
}
static void sub_hdmi_hpd_interrupt(enum gpio_signal s)
{
	int hdmi_hpd_odl = gpio_get_level(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL);

	gpio_set_level(GPIO_EC_AP_USB_C1_HDMI_HPD, !hdmi_hpd_odl);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = {
		.name = "TEMP_SENSOR1",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2] = {
		.name = "TEMP_SENSOR2",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SUB_ANALOG] = {
		.name = "SUB_ANALOG",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_VSNS_PP3300_A] = {
		.name = "PP3300_A_PGOOD",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static int board_id = -1;
static int mux_c1 = SSFC_USB_SS_MUX_DEFAULT;

extern const struct usb_mux_chain usbc0_retimer;
extern const struct usb_mux usbmux_ps8743;

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	check_c0_line();

	if (get_cbi_fw_config_db() == DB_1A_HDMI) {
		/* Disable i2c on HDMI pins */
		gpio_config_pin(MODULE_I2C, GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL,
				0);
		gpio_config_pin(MODULE_I2C, GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL,
				0);

		/* Set HDMI and sub-rail enables to output */
		gpio_set_flags(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL,
			       chipset_in_state(CHIPSET_STATE_ON) ?
				       GPIO_ODR_LOW :
				       GPIO_ODR_HIGH);
		gpio_set_flags(GPIO_SUB_C1_INT_EN_RAILS_ODL, GPIO_ODR_HIGH);

		/* Select HDMI option */
		gpio_set_level(GPIO_HDMI_SEL_L, 0);

		/* Enable interrupt for passing through HPD */
		gpio_enable_interrupt(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL);
	} else {
		/* Set SDA as an input */
		gpio_set_flags(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL, GPIO_INPUT);

		/* Enable C1 interrupts */
		gpio_enable_interrupt(GPIO_SUB_C1_INT_EN_RAILS_ODL);
		check_c1_line();
	}

	/* Turn on 5V if the system is on, otherwise turn it off. */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS) {
			board_id = val;
			if (board_id == 2) {
				nb7v904m_lpm_disable = 1;
				nb7v904m_set_aux_ch_switch(
					usbc0_retimer.mux,
					NB7V904M_AUX_CH_FLIPPED);
			}
		}
	}

	mux_c1 = get_cbi_ssfc_usb_ss_mux();

	if (mux_c1 == SSFC_USB_SS_MUX_PS8743) {
		usb_muxes[1].mux = &usbmux_ps8743;
		usb_muxes[1].next = NULL;
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Enable HDMI any time the SoC is on */
static void hdmi_enable(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		gpio_set_level(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, hdmi_enable, HOOK_PRIO_DEFAULT);

static void hdmi_disable(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		gpio_set_level(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hdmi_disable, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/*
	 * Both charger ICs need to be put into their "low power mode" before
	 * entering the Z-state.
	 */
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(1, true);
	raa489000_hibernate(0, false);
}

/* USB-A charging control */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_VBUS,
};

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b:147316511): Here we could issue a digital reset to the IC,
	 * unsure if we actually want to do that or not yet.
	 */
}

static void set_5v_gpio(int level)
{
	gpio_set_level(GPIO_EN_PP5000, level);
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Port 0 simply has a GPIO to turn on the 5V regulator, however, 5V is
	 * generated locally on the sub board and we need to set the comparator
	 * polarity on the sub board charger IC, or send enable signal to HDMI
	 * DB.
	 */
	set_5v_gpio(!!enable);

	if (get_cbi_fw_config_db() == DB_1A_HDMI) {
		gpio_set_level(GPIO_SUB_C1_INT_EN_RAILS_ODL, !enable);
	} else {
		if (isl923x_set_comparator_inversion(1, !!enable))
			CPRINTS("Failed to %sable sub rails!",
				enable ? "en" : "dis");
	}
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__override uint8_t board_get_charger_chip_count(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		return CHARGER_NUM - 1;
	else
		return CHARGER_NUM;
}

int board_is_sourcing_vbus(int port)
{
	int regval;

	tcpc_read(port, TCPC_REG_POWER_STATUS, &regval);
	return !!(regval & TCPC_REG_POWER_STATUS_SOURCING_VBUS);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < board_get_usb_pd_port_count());
	int i;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	CPRINTS("New chg p%d", port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(i, false);
		}

		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port)
			continue;

		if (tcpc_write(i, TCPC_REG_COMMAND,
			       TCPC_REG_COMMAND_SNK_CTRL_LOW))
			CPRINTS("p%d: sink path disable failed.", i);
		raa489000_enable_asgate(i, false);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (raa489000_enable_asgate(port, true) ||
	    tcpc_write(port, TCPC_REG_COMMAND,
		       TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		CPRINTS("p%d: sink path enable failed.", port);
		charger_discharge_on_ac(0);
		return EC_ERROR_UNKNOWN;
	}

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return EC_SUCCESS;
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if (port < 0 || port > board_get_usb_pd_port_count())
		return;

	raa489000_set_output_current(port, rp);
}

__override void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
				       int *ki_div, int *kd, int *kd_div)
{
	*kp = 1;
	*kp_div = 20;
	*ki = 1;
	*ki_div = 250;
	*kd = 0;
	*kd_div = 1;
}

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},

	{
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
		.flags = PI3USB9201_ALWAYS_POWERED,
	},

	{
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
		.flags = PI3USB9201_ALWAYS_POWERED,
	},
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	},

	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_SUB_USB_C1,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	},
};

static int board_nb7v904m_mux_set_c0(const struct usb_mux *me,
				     mux_state_t mux_state);
static int board_nb7v904m_mux_set(const struct usb_mux *me,
				  mux_state_t mux_state);
static int ps8743_tune_mux(const struct usb_mux *me);

const struct usb_mux_chain usbc0_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = 0,
			.i2c_port = I2C_PORT_USB_C0,
			.i2c_addr_flags = NB7V904M_I2C_ADDR0,
			.driver = &nb7v904m_usb_redriver_drv,
			.board_set = &board_nb7v904m_mux_set_c0,
		},
};
const struct usb_mux_chain usbc1_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = 1,
			.i2c_port = I2C_PORT_SUB_USB_C1,
			.i2c_addr_flags = NB7V904M_I2C_ADDR0,
			.driver = &nb7v904m_usb_redriver_drv,
			.board_set = &board_nb7v904m_mux_set,
		},
};

const struct usb_mux usbmux_ps8743 = {
	.usb_port = 1,
	.i2c_port = I2C_PORT_SUB_USB_C1,
	.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG,
	.driver = &ps8743_usb_mux_driver,
	.board_init = &ps8743_tune_mux,
};

struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
				.driver = &pi3usb3x532_usb_mux_driver,
			},
		.next = &usbc0_retimer,
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_SUB_USB_C1,
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
				.driver = &pi3usb3x532_usb_mux_driver,
			},
		.next = &usbc1_retimer,
	}
};
/* USB Mux C1 : board_init of PS8743 */
static int ps8743_tune_mux(const struct usb_mux *me)
{
	ps8743_tune_usb_eq(me, PS8743_USB_EQ_TX_3_6_DB,
			   PS8743_USB_EQ_RX_16_0_DB);

	return EC_SUCCESS;
}

/* USB Mux C0 */
static int board_nb7v904m_mux_set_c0(const struct usb_mux *me,
				     mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int flipped = !!(mux_state & USB_PD_MUX_POLARITY_INVERTED);

	if (board_id == -1) {
		uint32_t val;

		if (cbi_get_board_version(&val) == EC_SUCCESS)
			board_id = val;
		if (board_id == 2)
			nb7v904m_lpm_disable = 1;
	}

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			/* USB with DP */
			if (flipped) {
				rv |= nb7v904m_tune_usb_set_eq(
					me, NB7V904M_CH_A_EQ_10_DB,
					NB7V904M_CH_B_EQ_0_DB,
					NB7V904M_CH_C_EQ_2_DB,
					NB7V904M_CH_D_EQ_2_DB);
				rv |= nb7v904m_tune_usb_flat_gain(
					me, NB7V904M_CH_A_GAIN_0_DB,
					NB7V904M_CH_B_GAIN_1P5_DB,
					NB7V904M_CH_C_GAIN_0_DB,
					NB7V904M_CH_D_GAIN_0_DB);
				rv |= nb7v904m_set_loss_profile_match(
					me, NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_C,
					NB7V904M_LOSS_PROFILE_C);
			} else {
				rv |= nb7v904m_tune_usb_set_eq(
					me, NB7V904M_CH_A_EQ_2_DB,
					NB7V904M_CH_B_EQ_2_DB,
					NB7V904M_CH_C_EQ_0_DB,
					NB7V904M_CH_D_EQ_10_DB);
				rv |= nb7v904m_tune_usb_flat_gain(
					me, NB7V904M_CH_A_GAIN_0_DB,
					NB7V904M_CH_B_GAIN_0_DB,
					NB7V904M_CH_C_GAIN_1P5_DB,
					NB7V904M_CH_D_GAIN_0_DB);
				rv |= nb7v904m_set_loss_profile_match(
					me, NB7V904M_LOSS_PROFILE_C,
					NB7V904M_LOSS_PROFILE_C,
					NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_A);
			}
		} else {
			/* USB only */
			if (board_id == 2)
				rv |= nb7v904m_set_aux_ch_switch(
					me, NB7V904M_AUX_CH_FLIPPED);

			rv |= nb7v904m_tune_usb_set_eq(me,
						       NB7V904M_CH_A_EQ_10_DB,
						       NB7V904M_CH_B_EQ_0_DB,
						       NB7V904M_CH_C_EQ_0_DB,
						       NB7V904M_CH_D_EQ_10_DB);
			rv |= nb7v904m_tune_usb_flat_gain(
				me, NB7V904M_CH_A_GAIN_0_DB,
				NB7V904M_CH_B_GAIN_1P5_DB,
				NB7V904M_CH_C_GAIN_1P5_DB,
				NB7V904M_CH_D_GAIN_0_DB);
			rv |= nb7v904m_set_loss_profile_match(
				me, NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A);
		}

	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* 4 lanes DP */
		rv |= nb7v904m_tune_usb_set_eq(me, NB7V904M_CH_A_EQ_2_DB,
					       NB7V904M_CH_B_EQ_2_DB,
					       NB7V904M_CH_C_EQ_2_DB,
					       NB7V904M_CH_D_EQ_2_DB);
		rv |= nb7v904m_tune_usb_flat_gain(me, NB7V904M_CH_A_GAIN_0_DB,
						  NB7V904M_CH_B_GAIN_0_DB,
						  NB7V904M_CH_C_GAIN_0_DB,
						  NB7V904M_CH_D_GAIN_0_DB);
		rv |= nb7v904m_set_loss_profile_match(
			me, NB7V904M_LOSS_PROFILE_C, NB7V904M_LOSS_PROFILE_C,
			NB7V904M_LOSS_PROFILE_C, NB7V904M_LOSS_PROFILE_C);
	}

	return rv;
}

/* USB Mux */
static int board_nb7v904m_mux_set(const struct usb_mux *me,
				  mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int flipped = !!(mux_state & USB_PD_MUX_POLARITY_INVERTED);

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* USB with DP */
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			if (flipped) {
				rv |= nb7v904m_tune_usb_set_eq(
					me, NB7V904M_CH_A_EQ_10_DB,
					NB7V904M_CH_ALL_SKIP_EQ,
					NB7V904M_CH_ALL_SKIP_EQ,
					NB7V904M_CH_D_EQ_4_DB);
				rv |= nb7v904m_tune_usb_flat_gain(
					me, NB7V904M_CH_ALL_SKIP_GAIN,
					NB7V904M_CH_B_GAIN_3P5_DB,
					NB7V904M_CH_C_GAIN_0_DB,
					NB7V904M_CH_ALL_SKIP_GAIN);
				rv |= nb7v904m_set_loss_profile_match(
					me, NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_D,
					NB7V904M_LOSS_PROFILE_D);
			} else {
				rv |= nb7v904m_tune_usb_set_eq(
					me, NB7V904M_CH_A_EQ_4_DB,
					NB7V904M_CH_ALL_SKIP_EQ,
					NB7V904M_CH_ALL_SKIP_EQ,
					NB7V904M_CH_D_EQ_10_DB);
				rv |= nb7v904m_tune_usb_flat_gain(
					me, NB7V904M_CH_ALL_SKIP_GAIN,
					NB7V904M_CH_B_GAIN_0_DB,
					NB7V904M_CH_C_GAIN_3P5_DB,
					NB7V904M_CH_ALL_SKIP_GAIN);
				rv |= nb7v904m_set_loss_profile_match(
					me, NB7V904M_LOSS_PROFILE_D,
					NB7V904M_LOSS_PROFILE_D,
					NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_A);
			}
		} else {
			/* USB only */
			rv |= nb7v904m_tune_usb_set_eq(me,
						       NB7V904M_CH_A_EQ_10_DB,
						       NB7V904M_CH_ALL_SKIP_EQ,
						       NB7V904M_CH_ALL_SKIP_EQ,
						       NB7V904M_CH_D_EQ_10_DB);
			rv |= nb7v904m_tune_usb_flat_gain(
				me, NB7V904M_CH_ALL_SKIP_GAIN,
				NB7V904M_CH_B_GAIN_3P5_DB,
				NB7V904M_CH_C_GAIN_3P5_DB,
				NB7V904M_CH_ALL_SKIP_GAIN);
			rv |= nb7v904m_set_loss_profile_match(
				me, NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A);
		}

	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* 4 lanes DP */
		rv |= nb7v904m_tune_usb_set_eq(me, NB7V904M_CH_A_EQ_4_DB,
					       NB7V904M_CH_ALL_SKIP_EQ,
					       NB7V904M_CH_ALL_SKIP_EQ,
					       NB7V904M_CH_D_EQ_4_DB);
		rv |= nb7v904m_tune_usb_flat_gain(me, NB7V904M_CH_ALL_SKIP_GAIN,
						  NB7V904M_CH_B_GAIN_0_DB,
						  NB7V904M_CH_C_GAIN_0_DB,
						  NB7V904M_CH_ALL_SKIP_GAIN);
		rv |= nb7v904m_set_loss_profile_match(
			me, NB7V904M_LOSS_PROFILE_D, NB7V904M_LOSS_PROFILE_D,
			NB7V904M_LOSS_PROFILE_D, NB7V904M_LOSS_PROFILE_D);
	}

	return rv;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	/*
	 * The interrupt line is shared between the TCPC and BC1.2 detector IC.
	 * Therefore, go out and actually read the alert registers to report the
	 * alert status.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			/* The TCPCI Rev 1.0 spec says to ignore bits 14:12. */
			if (!(tcpc_config[0].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_0;
		}
	}

	if (board_get_usb_pd_port_count() > 1 &&
	    !gpio_get_level(GPIO_SUB_C1_INT_EN_RAILS_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			/* TCPCI spec Rev 1.0 says to ignore bits 14:12. */
			if (!(tcpc_config[1].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}

static const struct ec_response_keybd_config keybd1 = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_FORWARD,		/* T2 */
		TK_REFRESH,		/* T3 */
		TK_FULLSCREEN,		/* T4 */
		TK_OVERVIEW,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	/* No function keys, no numeric keypad and no screenlock key */
};

/* TK_REFRESH is always T3 above, vivaldi_keys are not overridden. */
BUILD_ASSERT_REFRESH_RC(2, 2);

BUILD_ASSERT(IS_ENABLED(CONFIG_KEYBOARD_VIVALDI));
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	/*
	 * Future boards should use fw_config if needed.
	 */

	return &keybd1;
}
