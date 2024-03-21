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
#include "driver/accel_kionix.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/retimer/tusb544.h"
#include "driver/tcpm/raa489000.h"
#include "driver/temp_sensor/thermistor.h"
#include "driver/usb_mux/it5205.h"
#include "gpio.h"
#include "hooks.h"
#include "intc.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

/* C0 interrupt line shared by BC 1.2 and charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

/* Use default keyboard scan config, because board didn't supply one */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * CONFIG_KEYBOARD_COL2_INVERTED is defined for passing the column 2
	 * to H1 which inverts the signal. The signal passing through H1
	 * adds more delay. Need a larger delay value. Otherwise, pressing
	 * Refresh key will also trigger T key, which is in the next scanning
	 * column line. See http://b/156007029.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xf6, 0x55, 0xfa, 0xca  /* full set */
	},
};

static const struct ec_response_keybd_config pirika_kb = {
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
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config pasara_kb = {
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
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};
__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (get_cbi_fw_config_numeric_pad() == NUMERIC_PAD_ABSENT)
		return &pirika_kb;
	else
		return &pasara_kb;
}

static void notify_c0_chips(void)
{
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
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL)) {
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
	[ADC_TEMP_SENSOR_3] = { .name = "TEMP_SENSOR3",
				.factor_mul = ADC_MAX_MVOLT,
				.factor_div = ADC_READ_MAX + 1,
				.shift = 0,
				.channel = CHIP_ADC_CH15 },
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

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

/* Charger chips */
const struct charger_config_t chg_chips[] = {
	[CHARGER_PRIMARY] = {
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
	[CHARGER_SECONDARY] = {
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

/* USB Retimer */
enum tusb544_conf { USB_DP = 0, USB_DP_INV, USB, USB_INV, DP, DP_INV };

static int board_tusb544_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	enum tusb544_conf usb_mode = 0;
	/* USB */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* USB with DP */
		if (mux_state & USB_PD_MUX_DP_ENABLED) {
			usb_mode = (mux_state & USB_PD_MUX_POLARITY_INVERTED) ?
					   USB_DP_INV :
					   USB_DP;
		}
		/* USB without DP */
		else {
			usb_mode = (mux_state & USB_PD_MUX_POLARITY_INVERTED) ?
					   USB_INV :
					   USB;
		}
	}
	/* DP without USB */
	else if (mux_state & USB_PD_MUX_DP_ENABLED) {
		usb_mode = (mux_state & USB_PD_MUX_POLARITY_INVERTED) ? DP_INV :
									DP;
	}
	/* Nothing enabled */
	else
		return EC_SUCCESS;
	/* Write the retimer config byte */
	if (usb_mode == USB_INV) {
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_GENERAL4, 0x15);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_1, 0x33);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_2, 0x33);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_1, 0x22);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_2, 0x22);
	} else if (usb_mode == USB) {
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_GENERAL4, 0x11);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_1, 0x33);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_2, 0x33);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_1, 0x22);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_2, 0x22);
	} else if (usb_mode == USB_DP_INV) {
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_GENERAL4, 0x1F);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_1, 0x33);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_2, 0x99);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_1, 0x22);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_2, 0x22);
	} else if (usb_mode == USB_DP) {
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_GENERAL4, 0x1B);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_1, 0x99);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_2, 0x33);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_1, 0x22);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_USB3_1_2, 0x22);
	} else if (usb_mode == DP_INV) {
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_GENERAL4, 0x1E);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_1, 0x99);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_2, 0x99);
	} else if (usb_mode == DP) {
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_GENERAL4, 0x1A);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_1, 0x99);
		rv = i2c_write8(me->i2c_port, me->i2c_addr_flags,
				TUSB544_REG_DISPLAYPORT_2, 0x99);
	}

	return rv;
}

const struct usb_mux_chain usbc1_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = 1,
			.i2c_port = I2C_PORT_SUB_USB_C1,
			.i2c_addr_flags = TUSB544_I2C_ADDR_FLAGS0,
			.driver = &tusb544_drv,
			.board_set = &board_tusb544_set,
		},
};

/* USB Muxes */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
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
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_SUB_USB_C1,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
			},
		.next = &usbc1_retimer,
	},
};

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_INT_ODL);

	/*
	 * If interrupt lines are already low, schedule them to be processed
	 * after inits are completed.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL))
		hook_call_deferred(&check_c0_line_data, 0);
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL))
		hook_call_deferred(&check_c1_line_data, 0);

	gpio_enable_interrupt(GPIO_USB_C0_CCSBU_OVP_ODL);
	/* Enable Base Accel interrupt */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);

	/* Turn on 5V if the system is on, otherwise turn it off */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	if (get_cbi_fw_config_numeric_pad() == NUMERIC_PAD_ABSENT) {
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
	} else {
		/* Setting scan mask KSO11, KSO12, KSO13 and KSO14 */
		keyscan_config.actual_key_mask[11] = 0xfe;
		keyscan_config.actual_key_mask[12] = 0xff;
		keyscan_config.actual_key_mask[13] = 0xff;
		keyscan_config.actual_key_mask[14] = 0xff;
	}

	/* Keyboard config = 1 : CA-FR US keyboard*/
	if (get_cbi_fw_config_keyboard() == 1) {
		/*
		 * Candian French keyboard (US Type).
		 * \|:		0x0061 -> 0x61 -> 0x56
		 * r-ctrl:	0xe014 -> 0x14 -> 0x1d
		 */
		uint16_t tmp = get_scancode_set2(4, 0);

		set_scancode_set2(4, 0, get_scancode_set2(2, 7));
		set_scancode_set2(2, 7, tmp);
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/*
	 * Put all charger ICs present into low power mode before entering
	 * z-state.
	 */
	raa489000_hibernate(CHARGER_PRIMARY, true);
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(CHARGER_SECONDARY, true);
}

__override void board_ocpc_init(struct ocpc_data *ocpc)
{
	if (get_cbi_fw_config_db() != DB_NONE)
		/* There's no provision to measure Isys */
		ocpc->chg_flags[CHARGER_SECONDARY] |= OCPC_NO_ISYS_MEAS_CAP;
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

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			if (regval)
				status = PD_STATUS_TCPC_ALERT_0;
		}
	}

	/* Check whether TCPC 1 pulled the shared interrupt line */
	if (!gpio_get_level(GPIO_USB_C1_INT_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			if (regval)
				status = PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
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
	 * Turn off the other ports sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (port == i)
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

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if (!board_is_usb_pd_port_present(port))
		return;

	raa489000_set_output_current(port, rp);
}

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP,
		.freq_hz = 10000,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Sensor Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Sensor Data */
static struct kionix_accel_data g_kx022_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* Drivers */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 2, /* g */
		/* We only use 2g because its resolution is only 8-bits */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 4,  /* g */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_GYRO),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = NULL,
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Charger",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Vcore",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
	[TEMP_SENSOR_3] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_3 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(68),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(50),
	},
};
const static struct ec_thermal_config thermal_vcore = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(53),
	},
};
const static struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(50),
	},
};
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1] = thermal_charger,
	[TEMP_SENSOR_2] = thermal_vcore,
	[TEMP_SENSOR_3] = thermal_ambient,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/* This callback disables keyboard when convertibles are fully open */
__override void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}

__override void board_pulse_entering_rw(void)
{
	/*
	 * On the ITE variants, the EC_ENTERING_RW signal was connected to a pin
	 * which is active high by default. This cause Cr50 to think that the
	 * EC has jumped to its RW image even though this may not be the case.
	 * The pin is changed to GPIO_EC_ENTERING_RW2.
	 */
	gpio_set_level(GPIO_EC_ENTERING_RW, 1);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 1);
	usleep(MSEC);
	gpio_set_level(GPIO_EC_ENTERING_RW, 0);
	gpio_set_level(GPIO_EC_ENTERING_RW2, 0);
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (get_cbi_fw_config_db() == DB_NONE)
		return 1;
	else
		return 2;
}

__override uint8_t board_get_charger_chip_count(void)
{
	if (get_cbi_fw_config_db() == DB_NONE)
		return 1;
	else
		return 2;
}

__override bool board_usb_charger_support(void)
{
	return (get_cbi_fw_config_bc_support() == BC12_SUPPORT);
}

enum battery_cell_type battery_cell;

static void get_battery_cell(void)
{
	int val;

	if (i2c_read16(I2C_PORT_USB_C0, ISL923X_ADDR_FLAGS, ISL9238_REG_INFO2,
		       &val) == EC_SUCCESS) {
		/* PROG resistor read out. Number of battery cells [4:0] */
		val = val & 0x001f;
	}

	if (val == 0 || val >= 0x18)
		battery_cell = BATTERY_CELL_TYPE_1S;
	else if (val >= 0x01 && val <= 0x08)
		battery_cell = BATTERY_CELL_TYPE_2S;
	else if (val >= 0x09 && val <= 0x10)
		battery_cell = BATTERY_CELL_TYPE_3S;
	else
		battery_cell = BATTERY_CELL_TYPE_4S;

	CPRINTS("Get battery cells: %d", battery_cell);
}
DECLARE_HOOK(HOOK_INIT, get_battery_cell, HOOK_PRIO_INIT_I2C + 1);

enum battery_cell_type board_get_battery_cell_type(void)
{
	return battery_cell;
}
