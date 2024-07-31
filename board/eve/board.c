/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Eve board-specific configuration */

#include "acpi.h"
#include "bd99992gw.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "compiler.h"
#include "console.h"
#include "device_event.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kxcj9.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_si114x.h"
#include "driver/charger/bd9995x.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/temp_sensor/bd99992gw.h"
#include "espi.h"
#include "extpower.h"
#include "gesture.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	switch (signal) {
	case GPIO_USB_C0_PD_INT_ODL:
		port = 0;
		break;
	case GPIO_USB_C1_PD_INT_ODL:
		port = 1;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

/*
 * enable_input_devices() is called by the tablet_mode ISR, but changes the
 * state of GPIOs, so its definition must reside after including gpio_list.
 */
static void enable_input_devices(void);
DECLARE_DEFERRED(enable_input_devices);

void tablet_mode_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&enable_input_devices_data, CONFIG_LID_DEBOUNCE_US);
}

/* Send event to wake AP based on trackpad input */
void trackpad_interrupt(enum gpio_signal signal)
{
	device_set_single_event(EC_DEVICE_EVENT_TRACKPAD);
}

/* Send event to wake AP based on DSP interrupt */
void dsp_interrupt(enum gpio_signal signal)
{
	device_set_single_event(EC_DEVICE_EVENT_DSP);
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void anx74xx_c0_cable_det_handler(void)
{
	int cable_det = gpio_get_level(GPIO_USB_C0_CABLE_DET);
	int reset_n = gpio_get_level(GPIO_USB_C0_PD_RST_L);

	/*
	 * A cable_det low->high transition was detected. If following the
	 * debounce time, cable_det is high, and reset_n is low, then ANX3429 is
	 * currently in standby mode and needs to be woken up. Set the
	 * TCPC_RESET event which will bring the ANX3429 out of standby
	 * mode. Setting this event is gated on reset_n being low because the
	 * ANX3429 will always set cable_det when transitioning to normal mode
	 * and if in normal mode, then there is no need to trigger a tcpc reset.
	 */
	if (cable_det && !reset_n)
		task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET);
}
DECLARE_DEFERRED(anx74xx_c0_cable_det_handler);

static void anx74xx_c1_cable_det_handler(void)
{
	int cable_det = gpio_get_level(GPIO_USB_C1_CABLE_DET);
	int reset_n = gpio_get_level(GPIO_USB_C1_PD_RST_L);

	/*
	 * A cable_det low->high transition was detected. If following the
	 * debounce time, cable_det is high, and reset_n is low, then ANX3429 is
	 * currently in standby mode and needs to be woken up. Set the
	 * TCPC_RESET event which will bring the ANX3429 out of standby
	 * mode. Setting this event is gated on reset_n being low because the
	 * ANX3429 will always set cable_det when transitioning to normal mode
	 * and if in normal mode, then there is no need to trigger a tcpc reset.
	 */
	if (cable_det && !reset_n)
		task_set_event(TASK_ID_PD_C1, PD_EVENT_TCPC_RESET);
}
DECLARE_DEFERRED(anx74xx_c1_cable_det_handler);

void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* Check if it is port 0 or 1, and debounce for 2 msec. */
	if (signal == GPIO_USB_C0_CABLE_DET)
		hook_call_deferred(&anx74xx_c0_cable_det_handler_data,
				   (2 * MSEC));
	else
		hook_call_deferred(&anx74xx_c1_cable_det_handler_data,
				   (2 * MSEC));
}
#endif

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* Keyboard scan. Increase output_settle_us to 80us from default 50us. */
__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { 5, 0, 10000 },
	[PWM_CH_LED_L_RED] = { 2, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_L_GREEN] = { 3, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_L_BLUE] = { 4, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_R_RED] = { 1, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_R_GREEN] = { 0, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_R_BLUE] = { 6, PWM_CONFIG_DSLEEP, 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Hibernate wake configuration */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 400,
	  .scl = GPIO_I2C0_0_SCL,
	  .sda = GPIO_I2C0_0_SDA },
	{ .name = "tcpc1",
	  .port = I2C_PORT_TCPC1,
	  .kbps = 400,
	  .scl = GPIO_I2C0_1_SCL,
	  .sda = GPIO_I2C0_1_SDA },
	{ .name = "accelgyro",
	  .port = I2C_PORT_GYRO,
	  .kbps = 400,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "sensors",
	  .port = I2C_PORT_LID_ACCEL,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "batt",
	  .port = I2C_PORT_BATTERY,
	  .kbps = 100,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = ANX74XX_I2C_ADDR1_FLAGS,
		},
		.drv = &anx74xx_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = ANX74XX_I2C_ADDR1_FLAGS,
		},
		.drv = &anx74xx_tcpm_drv,
	},
};

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.driver = &anx74xx_tcpm_usb_mux_driver,
				.hpd_update = &anx74xx_tcpc_update_hpd_status,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.driver = &anx74xx_tcpm_usb_mux_driver,
				.hpd_update = &anx74xx_tcpc_update_hpd_status,
			},
	},
};

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = BD9995X_ADDR_FLAGS,
		.drv = &bd9995x_drv,
	},
};

/**
 * Power on (or off) a single TCPC.
 * minimum on/off delays are included.
 *
 * @param port	Port number of TCPC.
 * @param mode	0: power off, 1: power on.
 */
void board_set_tcpc_power_mode(int port, int mode)
{
	switch (port) {
	case 0:
		if (mode) {
			gpio_set_level(GPIO_USB_C0_TCPC_PWR, 1);
			crec_msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
			gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
		} else {
			gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
			crec_msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
			gpio_set_level(GPIO_USB_C0_TCPC_PWR, 0);
			crec_msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
		}
		break;
	case 1:
		if (mode) {
			gpio_set_level(GPIO_USB_C1_TCPC_PWR, 1);
			crec_msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
			gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
		} else {
			gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
			crec_msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
			gpio_set_level(GPIO_USB_C1_TCPC_PWR, 0);
			crec_msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
		}
		break;
	}
}

void board_reset_pd_mcu(void)
{
	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	crec_msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
	/* Disable power */
	gpio_set_level(GPIO_USB_C0_TCPC_PWR, 0);
	gpio_set_level(GPIO_USB_C1_TCPC_PWR, 0);
	crec_msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	/* Enable power */
	gpio_set_level(GPIO_USB_C0_TCPC_PWR, 1);
	gpio_set_level(GPIO_USB_C1_TCPC_PWR, 1);
	crec_msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
	/* Deassert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
}

void board_tcpc_init(void)
{
	int count = 0;
	int port;

	/* Wait for disconnected battery to wake up */
	while (battery_hw_present() == BP_YES &&
	       battery_is_present() == BP_NO) {
		crec_usleep(100 * MSEC);
		/* Give up waiting after 2 seconds */
		if (++count > 20)
			break;
	}

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);
	gpio_enable_interrupt(GPIO_USB_C1_CABLE_DET);
#endif

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

const struct temp_sensor_t temp_sensors[] = {
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0 },

	/* These BD99992GW temp sensors are only readable in S0 */
	{ "Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM0 },
	{ "Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM1 },
	{ "DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM2 },
	{ "eMMC", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	  BD99992GW_ADC_CHANNEL_SYSTHERM3 },
	{ "Gyro", TEMP_SENSOR_TYPE_BOARD, bmi160_get_sensor_temp, BASE_GYRO },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Check if PMIC fault registers indicate VR fault. If yes, print out fault
 * register info to console. Additionally, set panic reason so that the OS can
 * check for fault register info by looking at offset 0x14(PWRSTAT1) and
 * 0x15(PWRSTAT2) in cros ec panicinfo.
 */
static void board_report_pmic_fault(const char *str)
{
	int vrfault, pwrstat1 = 0, pwrstat2 = 0;
	uint32_t info;

	/* RESETIRQ1 -- Bit 4: VRFAULT */
	if (i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x8, &vrfault) !=
	    EC_SUCCESS)
		return;

	if (!(vrfault & BIT(4)))
		return;

	/* VRFAULT has occurred, print VRFAULT status bits. */

	/* PWRSTAT1 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x16, &pwrstat1);

	/* PWRSTAT2 */
	i2c_read8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x17, &pwrstat2);

	CPRINTS("PMIC VRFAULT: %s", str);
	CPRINTS("PMIC VRFAULT: PWRSTAT1=0x%02x PWRSTAT2=0x%02x", pwrstat1,
		pwrstat2);

	/* Clear all faults -- Write 1 to clear. */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x8, BIT(4));
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x16, pwrstat1);
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x17, pwrstat2);

	/*
	 * Status of the fault registers can be checked in the OS by looking at
	 * offset 0x14(PWRSTAT1) and 0x15(PWRSTAT2) in cros ec panicinfo.
	 */
	info = ((pwrstat2 & 0xFF) << 8) | (pwrstat1 & 0xFF);
	panic_set_reason(PANIC_SW_PMIC_FAULT, info, 0);
}

static void board_pmic_init(void)
{
	board_report_pmic_fault("SYSJUMP");

	/* Clear power source events */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x04, 0xff);

	/* Disable power button shutdown timer */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x14, 0x00);

	/* Disable VCCIO in ALL_SYS_PWRGD for early boards */
	if (board_get_version() <= BOARD_VERSION_DVTB)
		i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x18, 0x80);

	if (system_jumped_late())
		return;

	/* DISCHGCNT2 - enable 100 ohm discharge on V3.3A and V1.8A */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3d, 0x05);

	/* DISCHGCNT3 - enable 100 ohm discharge on V1.00A */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3e, 0x04);

	/* Set CSDECAYEN / VCCIO decays to 0V at assertion of SLP_S0# */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x30, 0x7a);

	/*
	 * Set V100ACNT / V1.00A Control Register:
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x37, 0x1a);

	/*
	 * Set V085ACNT / V0.85A Control Register:
	 * Lower power mode = 0.7V.
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x38, 0x7a);

	/* VRMODECTRL - disable low-power mode for all rails */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x3b, 0x1f);
}
DECLARE_DEFERRED(board_pmic_init);

static void board_set_tablet_mode(void)
{
	int flipped_360_mode = !gpio_get_level(GPIO_TABLET_MODE_L);

	tablet_set_mode(flipped_360_mode, TABLET_TRIGGER_LID);

	/* Update DPTF profile based on mode */
	if (flipped_360_mode)
		acpi_dptf_set_profile_num(DPTF_PROFILE_FLIPPED_360_MODE);
	else
		acpi_dptf_set_profile_num(DPTF_PROFILE_CLAMSHELL);
}

int board_has_working_reset_flags(void)
{
	int version = board_get_version();

	/* board version P1b to EVTb will lose reset flags on power cycle */
	if (version >= BOARD_VERSION_P1B && version <= BOARD_VERSION_EVTB)
		return 0;

	/* All other board versions should have working reset flags */
	return 1;
}

/* Initialize board. */
static void board_init(void)
{
	/* Enabure tablet mode is initialized */
	board_set_tablet_mode();

	/* Enable tablet mode interrupt for input device enable */
	gpio_enable_interrupt(GPIO_TABLET_MODE_L);

	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_CHARGER_INT_L);

	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACCELGYRO3_INT_L);

	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());

#ifndef TEST_BUILD
	if (board_get_version() == BOARD_VERSION_EVT) {
		/* Set F13 to new defined key on EVT */
		CPRINTS("Overriding F13 scan code");
		set_scancode_set2(3, 9, 0xe007);
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(3, 9, KLLI_F13);
#endif
	}
#endif

	/* Initialize PMIC */
	hook_call_deferred(&board_pmic_init_data, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override enum pd_dual_role_states pd_get_drp_state_in_suspend(void)
{
	/*
	 * If board is not connected to charger it will disable VBUS
	 * on all ports that acts as source when going to suspend.
	 * Change DRP state to force sink, to inform TCPM about that.
	 */
	if (!extpower_is_present())
		return PD_DRP_FORCE_SINK;

	return PD_DRP_TOGGLE_OFF;
}

/**
 * Buffer the AC present GPIO to the PCH.
 * Set appropriate DRP state when chipset in suspend
 */
static void board_extpower(void)
{
	enum pd_dual_role_states drp_state;
	int port;

	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());

	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_SUSPEND)) {
		drp_state = pd_get_drp_state_in_suspend();
		for (port = 0; port < board_get_usb_pd_port_count(); port++)
			if (pd_get_dual_role(port) != drp_state)
				pd_set_dual_role(port, drp_state);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

int pd_snk_is_vbus_provided(int port)
{
	if (port != 0 && port != 1)
		panic("Invalid charge port\n");

	return bd9995x_is_vbus_provided(port);
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	enum bd9995x_charge_port bd9995x_port;
	int bd9995x_port_select = 1;

	switch (charge_port) {
	case 0:
	case 1:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;

		bd9995x_port = charge_port;
		break;
	case CHARGE_PORT_NONE:
		bd9995x_port_select = 0;
		bd9995x_port = BD9995X_CHARGE_PORT_BOTH;

		/*
		 * To avoid inrush current from the external charger,
		 * enable discharge on AC until the new charger is detected
		 * and charge detect delay has passed.
		 */
		if (charge_get_percent() > 2)
			charger_discharge_on_ac(1);
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	CPRINTS("New chg p%d", charge_port);

	return bd9995x_select_input_port(bd9995x_port, bd9995x_port_select);
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/* Enable charging trigger by BC1.2 detection */
	int bc12_enable = (supplier == CHARGE_SUPPLIER_BC12_CDP ||
			   supplier == CHARGE_SUPPLIER_BC12_DCP ||
			   supplier == CHARGE_SUPPLIER_BC12_SDP ||
			   supplier == CHARGE_SUPPLIER_OTHER);

	if (bd9995x_bc12_enable_charging(port, bc12_enable))
		return;

	charge_set_input_current_limit(charge_ma, charge_mv);
}

/**
 * Return if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;

	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	return voltage < BD9995X_BC12_MIN_VOLTAGE;
}

/* Clear pending interrupts and enable DSP for wake */
static void dsp_wake_enable(int enable)
{
	if (enable) {
		gpio_clear_pending_interrupt(GPIO_MIC_DSP_IRQ_1V8_L);
		gpio_enable_interrupt(GPIO_MIC_DSP_IRQ_1V8_L);
	} else {
		gpio_disable_interrupt(GPIO_MIC_DSP_IRQ_1V8_L);
	}
}

/* Clear pending interrupts and enable trackpad for wake */
static void trackpad_wake_enable(int enable)
{
	static int prev_enable = -1;

	if (prev_enable == enable)
		return;
	prev_enable = enable;

	if (enable) {
		gpio_clear_pending_interrupt(GPIO_TRACKPAD_INT_L);
		gpio_enable_interrupt(GPIO_TRACKPAD_INT_L);
	} else {
		gpio_disable_interrupt(GPIO_TRACKPAD_INT_L);
	}
}

/* Enable or disable input devices, based upon chipset state and tablet mode */
static void enable_input_devices(void)
{
	/* We need to turn on tablet mode for motion sense */
	board_set_tablet_mode();

	/*
	 * Then, we disable peripherals only when the lid reaches 360 position.
	 * (It's probably already disabled by motion_sense_task.)
	 * We deliberately do not enable peripherals when the lid is leaving
	 * 360 position. Instead, we let motion_sense_task enable it once it
	 * reaches laptop zone (180 or less).
	 */
	if (tablet_get_mode())
		lid_angle_peripheral_enable(0);
}

/* Enable or disable input devices, based on chipset state and tablet mode */
__override void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If suspended and the lid is in 360 position, ignore the lid angle,
	 * which might be faulty. Disable keyboard and trackpad wake.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) ||
	    (tablet_get_mode() && chipset_in_state(CHIPSET_STATE_SUSPEND)))
		enable = 0;
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);

	/* Also disable trackpad wake if not in suspend */
	if (!chipset_in_state(CHIPSET_STATE_SUSPEND))
		enable = 0;
	trackpad_wake_enable(enable);
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	/* Enable Trackpad */
	gpio_set_level(GPIO_TRACKPAD_SHDN_L, 1);
	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable Trackpad and DSP wake in S5 */
	trackpad_wake_enable(0);
	dsp_wake_enable(0);
	gpio_set_level(GPIO_TRACKPAD_SHDN_L, 0);
	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
	if (lid_is_open()) {
		/* Enable DSP wake if suspended with lid open */
		dsp_wake_enable(1);

		/* Enable trackpad wake if suspended and not in tablet mode */
		if (!tablet_get_mode())
			trackpad_wake_enable(1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
	dsp_wake_enable(0);
	trackpad_wake_enable(0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_reset(void)
{
	board_report_pmic_fault("CHIPSET RESET");
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, board_chipset_reset, HOOK_PRIO_DEFAULT);

/* Called on lid change */
static void board_lid_change(void)
{
	/* Disable trackpad and DSP wake if lid is closed */
	if (!lid_is_open()) {
		trackpad_wake_enable(0);
		dsp_wake_enable(0);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, board_lid_change, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/* Enable both the VBUS & VCC ports before entering PG3 */
	bd9995x_select_input_port(BD9995X_CHARGE_PORT_BOTH, 1);

	/* Turn BGATE OFF for power saving */
	bd9995x_set_power_save_mode(BD9995X_PWR_SAVE_MAX);

	/* Shut down PMIC */
	CPRINTS("Triggering PMIC shutdown");
	uart_flush_output();
	if (i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992_FLAGS, 0x49, 0x01)) {
		/*
		 * If we can't tell the PMIC to shutdown, instead reset
		 * and don't start the AP. Hopefully we'll be able to
		 * communicate with the PMIC next time.
		 */
		CPRINTS("PMIC I2C failed");
		uart_flush_output();
		system_reset(SYSTEM_RESET_LEAVE_AP_OFF);
	}
	while (1)
		;
}

int board_get_version(void)
{
	static int ver;

	if (!ver) {
		/*
		 * Read the board EC ID on the tristate strappings
		 * using ternary encoding: 0 = 0, 1 = 1, Hi-Z = 2
		 */
		uint8_t id0, id1, id2;

		id0 = gpio_get_ternary(GPIO_BOARD_VERSION1);
		id1 = gpio_get_ternary(GPIO_BOARD_VERSION2);
		id2 = gpio_get_ternary(GPIO_BOARD_VERSION3);

		ver = (id2 * 9) + (id1 * 3) + id0;
		CPRINTS("Board ID = %d", ver);
	}

	return ver;
}

void sensor_board_proc_double_tap(void)
{
	led_register_double_tap();
}

/* Base Sensor mutex */
static struct mutex g_base_mutex;

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

static struct kionix_accel_data g_kxcj9_data;
static struct bmi_drv_data_t g_bmi160_data;

static struct si114x_drv_data_t
	g_si114x_data = { .state = SI114X_NOT_READY,
			  .covered = 0,
			  .type_data = {
				  /* Proximity - unused */
				  {},
				  /* light */
				  {
					  .base_data_reg = SI114X_ALS_VIS_DATA0,
					  .irq_flags =
						  SI114X_IRQ_ENABLE_ALS_IE_INT0 |
						  SI114X_IRQ_ENABLE_ALS_IE_INT1,
					  .scale = 1,
					  .offset = -256,
				  } } };

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t mag_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(1), 0 },
				      { 0, 0, FLOAT_TO_FP(-1) } };

const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kxcj9_data,
	 .port = I2C_PORT_LID_ACCEL,
	 .i2c_spi_addr_flags = KXCJ9_ADDR0_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2, /* g, enough for lid angle calculation. */
	 .min_frequency = KXCJ9_ACCEL_MIN_FREQ,
	 .max_frequency = KXCJ9_ACCEL_MAX_FREQ,
	 .config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		 /* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = TAP_ODR,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for lid angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = TAP_ODR,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on in S5 for battery detection */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = TAP_ODR,
			.ec_rate = 100 * MSEC,
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = NULL,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},

	[BASE_MAG] = {
	 .name = "Base Mag",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_MAG,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = BIT(11), /* 16LSB / uT, fixed */
	 .rot_standard_ref = &mag_standard_ref,
	 .min_frequency = BMM150_MAG_MIN_FREQ,
/* TODO(b/253292373): Remove when clang is fixed. */
DISABLE_CLANG_WARNING("-Wshift-count-negative")
	 .max_frequency = BMM150_MAG_MAX_FREQ(SPECIAL),
ENABLE_CLANG_WARNING("-Wshift-count-negative")
	},

	[LID_LIGHT] = {
	 .name = "Light",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_SI1141,
	 .type = MOTIONSENSE_TYPE_LIGHT,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &si114x_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_si114x_data,
	 .port = I2C_PORT_ALS,
	 .i2c_spi_addr_flags = SI114X_ADDR_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 6000, /* 60.00%: int = 0 - frac = 6000/10000 */
	 .min_frequency = SI114X_LIGHT_MIN_FREQ,
	 .max_frequency = SI114X_LIGHT_MAX_FREQ,
	 .config = {
		 /* Run ALS sensor in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 1000,
		 },
	 },
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[LID_LIGHT],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);
