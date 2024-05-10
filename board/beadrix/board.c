/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledee board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/retimer/nb7v904m.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "driver/usb_mux/pi3usb3x532.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)

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
	if (!gpio_get_level(GPIO_USB_C1_INT_V1_ODL)) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

static void usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
}

static void c0_ccsbu_ovp_interrupt(enum gpio_signal s)
{
	cprints(CC_USBPD, "C0: CC OVP, SBU OVP, or thermal event");
	pd_handle_cc_overvoltage(0);
}

__override void board_pulse_entering_rw(void)
{
	/*
	 * On the ITE variants, the EC_ENTERING_RW signal was connected to a pin
	 * which is active high by default.  This causes Cr50 to think that the
	 * EC has jumped to its RW image even though this may not be the case.
	 * The pin is changed to GPIO_EC_ENTERING_RW2.
	 */
	gpio_set_level(GPIO_EC_ENTERING_RW, 1);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 1);
	crec_usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 0);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_VSNS_PP3300_A] = { .name = "PP3300_A_PGOOD",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH0 },
	[ADC_TEMP_SENSOR_1] = { .name = "TEMP_SENSOR1",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH2 },
	[ADC_TEMP_SENSOR_2] = { .name = "TEMP_SENSOR2",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH3 },
	[ADC_SUB_ANALOG] = { .name = "SUB_ANALOG",
			     .factor_mul = ADC_MAX_MVOLT,
			     .factor_div = ADC_READ_MAX + 1,
			     .shift = 0,
			     .channel = CHIP_ADC_CH13 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* BC 1.2 chips */
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

/* Charger chips */
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

/* TCPCs */
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
	{	/* Used as TCPC + Charger */
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_SUB_USB_C1,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	},
};

static int board_nb7v904m_mux_set(const struct usb_mux *me,
				  mux_state_t mux_state);

/* USB Retimer */
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

/* USB Muxes */
struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_C0,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
			},
	},
	{
		/* Used as MUX only*/
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_SUB_USB_C1,
				.i2c_addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
				.flags = USB_MUX_FLAG_NOT_TCPC,
				.driver = &anx7447_usb_mux_driver,
			},
		.next = &usbc1_retimer,
	},
};

/* USB Mux */
static int board_nb7v904m_mux_set(const struct usb_mux *me,
				  mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	int flipped = !!(mux_state & USB_PD_MUX_POLARITY_INVERTED);

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* USB with DP */
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			if (flipped) { /* CC2 */
				rv |= nb7v904m_tune_usb_set_eq(
					me, NB7V904M_CH_A_EQ_0_DB,
					NB7V904M_CH_B_EQ_4_DB,
					NB7V904M_CH_C_EQ_0_DB,
					NB7V904M_CH_D_EQ_0_DB);
				rv |= nb7v904m_tune_usb_flat_gain(
					me, NB7V904M_CH_A_GAIN_0_DB,
					NB7V904M_CH_B_GAIN_3P5_DB,
					NB7V904M_CH_C_GAIN_0_DB,
					NB7V904M_CH_D_GAIN_0_DB);
				rv |= nb7v904m_set_loss_profile_match(
					me, NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_D,
					NB7V904M_LOSS_PROFILE_D);
			} /* CC1 */
			else {
				rv |= nb7v904m_tune_usb_set_eq(
					me, NB7V904M_CH_A_EQ_0_DB,
					NB7V904M_CH_B_EQ_0_DB,
					NB7V904M_CH_C_EQ_4_DB,
					NB7V904M_CH_D_EQ_0_DB);
				rv |= nb7v904m_tune_usb_flat_gain(
					me, NB7V904M_CH_A_GAIN_0_DB,
					NB7V904M_CH_B_GAIN_0_DB,
					NB7V904M_CH_C_GAIN_3P5_DB,
					NB7V904M_CH_D_GAIN_0_DB);
				rv |= nb7v904m_set_loss_profile_match(
					me, NB7V904M_LOSS_PROFILE_D,
					NB7V904M_LOSS_PROFILE_D,
					NB7V904M_LOSS_PROFILE_A,
					NB7V904M_LOSS_PROFILE_A);
			}
		} else {
			/* USB only */
			rv |= nb7v904m_tune_usb_set_eq(me,
						       NB7V904M_CH_A_EQ_0_DB,
						       NB7V904M_CH_B_EQ_4_DB,
						       NB7V904M_CH_C_EQ_4_DB,
						       NB7V904M_CH_D_EQ_0_DB);
			rv |= nb7v904m_tune_usb_flat_gain(
				me, NB7V904M_CH_A_GAIN_0_DB,
				NB7V904M_CH_B_GAIN_3P5_DB,
				NB7V904M_CH_C_GAIN_3P5_DB,
				NB7V904M_CH_D_GAIN_0_DB);
			rv |= nb7v904m_set_loss_profile_match(
				me, NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A,
				NB7V904M_LOSS_PROFILE_A);
		}

	} else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* 4 lanes DP */
		rv |= nb7v904m_tune_usb_set_eq(me, NB7V904M_CH_A_EQ_0_DB,
					       NB7V904M_CH_B_EQ_0_DB,
					       NB7V904M_CH_C_EQ_0_DB,
					       NB7V904M_CH_D_EQ_0_DB);
		rv |= nb7v904m_tune_usb_flat_gain(me, NB7V904M_CH_A_GAIN_0_DB,
						  NB7V904M_CH_B_GAIN_0_DB,
						  NB7V904M_CH_C_GAIN_0_DB,
						  NB7V904M_CH_D_GAIN_0_DB);
		rv |= nb7v904m_set_loss_profile_match(
			me, NB7V904M_LOSS_PROFILE_D, NB7V904M_LOSS_PROFILE_D,
			NB7V904M_LOSS_PROFILE_D, NB7V904M_LOSS_PROFILE_D);
	}

	return rv;
}

void board_init(void)
{
	int on;

	/* Enable C0 interrupt and check if it needs processing */
	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);

	if (get_cbi_fw_config_db() != DB_NONE) {
		/* Enable C1 interrupt and check if it needs processing */
		gpio_enable_interrupt(GPIO_USB_C1_INT_V1_ODL);
		check_c1_line();
	}

	/*
	 * If interrupt lines are already low, schedule them to be processed
	 * after inits are completed.
	 */
	check_c0_line();

	gpio_enable_interrupt(GPIO_USB_C0_CCSBU_OVP_ODL);

	/* Turn on 5V if the system is on, otherwise turn it off. */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/*
	 * Both charger ICs need to be put into their "low power mode" before
	 * entering the Z-state.
	 */
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(1, true);
	raa489000_hibernate(0, true);
}

void board_reset_pd_mcu(void)
{
	/*
	 * Nothing to do.  TCPC C0 is internal, TCPC C1 reset pin is not
	 * connected to the EC.
	 */
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Motherboard has a GPIO to turn on the 5V regulator, but the sub-board
	 * sets it through the charger GPIO.
	 */
	gpio_set_level(GPIO_EN_PP5000, !!enable);
	gpio_set_level(GPIO_EN_USB_A0_VBUS, !!enable);
	if ((get_cbi_fw_config_db() != DB_NONE) &&
	    (isl923x_set_comparator_inversion(1, !!enable)))
		CPRINTS("Failed to %sable sub rails!", enable ? "en" : "dis");
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

	CPRINTS("Old chg p%d", old_port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		CPRINTS("Disabling all charge ports");

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(i, false);
		}

		return EC_SUCCESS;
	}

	CPRINTS("New chg p%d", port);

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	if (old_port != CHARGE_PORT_NONE && old_port != port) {
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (i == port)
				continue;

			if (tcpc_write(i, TCPC_REG_COMMAND,
				       TCPC_REG_COMMAND_SNK_CTRL_LOW))
				CPRINTS("p%d: sink path disable failed.", i);
			raa489000_enable_asgate(i, false);
		}

		/*
		 * Stop the charger IC from switching while changing ports.
		 * Otherwise, we can overcurrent the adapter we're switching to.
		 * (crbug.com/926056)
		 */
		charger_discharge_on_ac(1);
	}

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
	if (port < 0 || port > CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	raa489000_set_output_current(port, rp);
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
	    !gpio_get_level(GPIO_USB_C1_INT_V1_ODL)) {
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

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = { [PWM_CH_KBLIGHT] = {
					      .channel = 0,
					      .flags = PWM_CONFIG_DSLEEP,
					      .freq_hz = 10000,
				      } };
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static const struct ec_response_keybd_config keybd1 = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	/* No function keys, no numeric keypad and no screenlock key */
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	/*
	 * Future boards should use fw_config if needed.
	 */

	return &keybd1;
};

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (get_cbi_fw_config_db() == DB_NONE)
		return 1;
	else
		return 2;
};

__override uint8_t board_get_charger_chip_count(void)
{
	if (get_cbi_fw_config_db() == DB_NONE)
		return 1;
	else
		return 2;
}
