/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Coral board-specific configuration */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/als_opt3001.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/charger/bd9995x.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "sku.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define IN_ALL_SYS_PG POWER_SIGNAL_MASK(X86_ALL_SYS_PG)
#define IN_PGOOD_PP3300 POWER_SIGNAL_MASK(X86_PGOOD_PP3300)
#define IN_PGOOD_PP5000 POWER_SIGNAL_MASK(X86_PGOOD_PP5000)

#define USB_PD_PORT_ANX74XX 0
#define USB_PD_PORT_PS8751 1

static int sku_id;

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

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void anx74xx_cable_det_handler(void)
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
DECLARE_DEFERRED(anx74xx_cable_det_handler);

void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* debounce for 2 msec */
	hook_call_deferred(&anx74xx_cable_det_handler_data, (2 * MSEC));
}
#endif

/*
 * enable_input_devices() is called by the tablet_mode ISR, but changes the
 * state of GPIOs, so its definition must reside after including gpio_list.
 * Use DECLARE_DEFERRED to generate enable_input_devices_data.
 */
static void enable_input_devices(void);
DECLARE_DEFERRED(enable_input_devices);

void tablet_mode_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&enable_input_devices_data, LID_DEBOUNCE_US);
}

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vfs = Vref = 2.816V, 10-bit unsigned reading */
	[ADC_TEMP_SENSOR_CHARGER] = { "CHARGER", NPCX_ADC_CH0, ADC_MAX_VOLT,
				      ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_AMB] = { "AMBIENT", NPCX_ADC_CH1, ADC_MAX_VOLT,
				  ADC_READ_MAX + 1, 0 },
	[ADC_BOARD_ID] = { "BRD_ID", NPCX_ADC_CH2, ADC_MAX_VOLT,
			   ADC_READ_MAX + 1, 0 },
	[ADC_BOARD_SKU_1] = { "BRD_SKU_1", NPCX_ADC_CH3, ADC_MAX_VOLT,
			      ADC_READ_MAX + 1, 0 },
	[ADC_BOARD_SKU_0] = { "BRD_SKU_0", NPCX_ADC_CH4, ADC_MAX_VOLT,
			      ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { 4, PWM_CONFIG_DSLEEP, 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct i2c_port_t i2c_ports[] = {
	{ .name = "tcpc0",
	  .port = NPCX_I2C_PORT0_0,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_USB_C0_PD_SCL,
	  .sda = GPIO_EC_I2C_USB_C0_PD_SDA },
	{ .name = "tcpc1",
	  .port = NPCX_I2C_PORT0_1,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_USB_C1_PD_SCL,
	  .sda = GPIO_EC_I2C_USB_C1_PD_SDA },
	{ .name = "accelgyro",
	  .port = I2C_PORT_GYRO,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_GYRO_SCL,
	  .sda = GPIO_EC_I2C_GYRO_SDA },
	{ .name = "sensors",
	  .port = NPCX_I2C_PORT2,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C_SENSOR_SCL,
	  .sda = GPIO_EC_I2C_SENSOR_SDA },
	{ .name = "batt",
	  .port = NPCX_I2C_PORT3,
	  .kbps = 100,
	  .scl = GPIO_EC_I2C_POWER_SCL,
	  .sda = GPIO_EC_I2C_POWER_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef CONFIG_CMD_I2C_STRESS_TEST
struct i2c_stress_test i2c_stress_tests[] = {
/* NPCX_I2C_PORT0_0 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
	{
		.port = NPCX_I2C_PORT0_0,
		.addr_flags = ANX74XX_I2C_ADDR1_FLAGS,
		.i2c_test = &anx74xx_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT0_1 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
	{
		.port = NPCX_I2C_PORT0_1,
		.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		.i2c_test = &ps8xxx_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT1 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
	{
		.port = I2C_PORT_GYRO,
		.addr_flags = BMI160_ADDR0_FLAGS,
		.i2c_test = &bmi160_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT2 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_ACCEL
	{
		.port = I2C_PORT_LID_ACCEL,
		.addr_flags = KX022_ADDR1_FLAGS,
		.i2c_test = &kionix_i2c_stress_test_dev,
	},
#endif

/* NPCX_I2C_PORT3 */
#ifdef CONFIG_CMD_I2C_STRESS_TEST_BATTERY
	{
		.i2c_test = &battery_i2c_stress_test_dev,
	},
#endif
#ifdef CONFIG_CMD_I2C_STRESS_TEST_CHARGER
	{
		.i2c_test = &bd9995x_i2c_stress_test_dev,
	},
#endif
};
const int i2c_test_dev_used = ARRAY_SIZE(i2c_stress_tests);
#endif /* CONFIG_CMD_I2C_STRESS_TEST */

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_0,
			.addr_flags = ANX74XX_I2C_ADDR1_FLAGS,
		},
		.drv = &anx74xx_tcpm_drv,
	},
	[USB_PD_PORT_PS8751] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = NPCX_I2C_PORT0_1,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
	},
};

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = BD9995X_ADDR_FLAGS,
		.drv = &bd9995x_drv,
	},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

static int ps8751_tune_mux(const struct usb_mux *me)
{
	/* 0x98 sets lower EQ of DP port (4.5db) */
	mux_write(me, PS8XXX_REG_MUX_DP_EQ_CONFIGURATION, 0x98);
	return EC_SUCCESS;
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_ANX74XX,
			.driver = &anx74xx_tcpm_usb_mux_driver,
			.hpd_update = &anx74xx_tcpc_update_hpd_status,
		},
	},
	[USB_PD_PORT_PS8751] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_PS8751,
			.driver = &tcpci_tcpm_usb_mux_driver,
			.hpd_update = &ps8xxx_tcpc_update_hpd_status,
			.board_init = &ps8751_tune_mux,
		},
	}
};

const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_USB1_ENABLE,
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
	if (port != USB_PD_PORT_ANX74XX)
		return;

	switch (mode) {
	case ANX74XX_NORMAL_MODE:
		gpio_set_level(GPIO_EN_USB_TCPC_PWR, 1);
		msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
		break;
	case ANX74XX_STANDBY_MODE:
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
		msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
		gpio_set_level(GPIO_EN_USB_TCPC_PWR, 0);
		msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
		break;
	default:
		break;
	}
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/* Assert reset to TCPC1 (ps8751) */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);

	/* Assert reset to TCPC0 (anx3429) */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);

	/* TCPC1 (ps8751) requires 1ms reset down assertion */
	msleep(MAX(1, ANX74XX_RST_L_PWR_L_DELAY_MS));

	/* Deassert reset to TCPC1 */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);
	/* Disable TCPC0 power */
	gpio_set_level(GPIO_EN_USB_TCPC_PWR, 0);

	/*
	 * anx3429 requires 10ms reset/power down assertion
	 */
	msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	board_set_tcpc_power_mode(USB_PD_PORT_ANX74XX, 1);
}

static void board_tcpc_init(void)
{
	int reg;
	int count = 0;

	/* Wait for disconnected battery to wake up */
	while (battery_hw_present() == BP_YES &&
	       battery_is_present() == BP_NO) {
		usleep(100 * MSEC);
		/* Give up waiting after 2 seconds */
		if (++count > 20)
			break;
	}

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_late())
		board_reset_pd_mcu();

	/*
	 * TODO: Remove when Coral is updated with PS8751 A3.
	 *
	 * Force PS8751 A2 to wake from low power mode.
	 * If PS8751 remains in low power mode after sysjump,
	 * TCPM_INIT will fail due to not able to access PS8751.
	 *
	 * NOTE: PS8751 A3 will wake on any I2C access.
	 */
	i2c_read8(NPCX_I2C_PORT0_1, 0x08, 0xA0, &reg);

	/* Enable TCPC0 interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable TCPC1 interrupt */
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);
#endif
	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port)
		usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
						 USB_PD_MUX_HPD_IRQ_DEASSERTED);
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_DEFAULT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_BATTERY] = { .name = "Battery",
				  .type = TEMP_SENSOR_TYPE_BATTERY,
				  .read = charge_get_battery_temp,
				  .idx = 0 },
	[TEMP_SENSOR_AMBIENT] = { .name = "Ambient",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_51k1_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_AMB },
	[TEMP_SENSOR_CHARGER] = { .name = "Charger",
				  .type = TEMP_SENSOR_TYPE_BOARD,
				  .read = get_temp_3v3_13k7_47k_4050b,
				  .idx = ADC_TEMP_SENSOR_CHARGER },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Called by APL power state machine when transitioning from G3 to S5 */
void chipset_pre_init_callback(void)
{
	/*
	 * No need to re-init PMIC since settings are sticky across sysjump.
	 * However, be sure to check that PMIC is already enabled. If it is
	 * then there's no need to re-sequence the PMIC.
	 */
	if (system_jumped_to_this_image() && gpio_get_level(GPIO_PMIC_EN))
		return;

	/* Enable PP5000 before PP3300 due to NFC: chrome-os-partner:50807 */
	gpio_set_level(GPIO_EN_PP5000, 1);
	while (!gpio_get_level(GPIO_PP5000_PG))
		;

	/*
	 * To prevent SLP glitches, PMIC_EN (V5A_EN) should be enabled
	 * at the same time as PP3300 (chrome-os-partner:51323).
	 */
	/* Enable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 1);
	while (!gpio_get_level(GPIO_PP3300_PG))
		;

	/* Enable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 1);
}

static void board_set_tablet_mode(void)
{
	int tablet_mode = 0;

	if (SKU_IS_CONVERTIBLE(sku_id))
		tablet_mode = !gpio_get_level(GPIO_TABLET_MODE_L);

	tablet_set_mode(tablet_mode, TABLET_TRIGGER_LID);
}

/* Initialize board. */
static void board_init(void)
{
	/* Ensure tablet mode is initialized according to the hardware state
	 * so that the cached state reflects reality. */
	board_set_tablet_mode();

	gpio_enable_interrupt(GPIO_TABLET_MODE_L);

	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_CHARGER_INT_L);

	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);

	/* Need to read SKU ID at least once each boot */
	sku_id = BOARD_VERSION_UNKNOWN;
}
/* PP3300 needs to be enabled before TCPC init hooks */
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_FIRST);

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
	case USB_PD_PORT_ANX74XX:
	case USB_PD_PORT_PS8751:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;

		bd9995x_port = charge_port;
		break;
	case CHARGE_PORT_NONE:
		bd9995x_port_select = 0;
		bd9995x_port = BD9995X_CHARGE_PORT_BOTH;

		/*
		 * To avoid inrush current from the external charger, enable
		 * discharge on AC till the new charger is detected and
		 * charge detect delay has passed.
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
void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	/* Enable charging trigger by BC1.2 detection */
	int bc12_enable = (supplier == CHARGE_SUPPLIER_BC12_CDP ||
			   supplier == CHARGE_SUPPLIER_BC12_DCP ||
			   supplier == CHARGE_SUPPLIER_BC12_SDP ||
			   supplier == CHARGE_SUPPLIER_OTHER);

	if (bd9995x_bc12_enable_charging(port, bc12_enable))
		return;

	charge_ma = (charge_ma * 95) / 100;
	charge_set_input_current_limit(
		MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
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

static void enable_input_devices(void)
{
	/* We need to turn on tablet mode for motion sense */
	board_set_tablet_mode();

	/* Then, we disable peripherals only when the lid reaches 360 position.
	 * (It's probably already disabled by motion_sense_task.)
	 * We deliberately do not enable peripherals when the lid is leaving
	 * 360 position. Instead, we let motion_sense_task enable it once it
	 * reaches laptop zone (180 or less). */
	if (tablet_get_mode())
		lid_angle_peripheral_enable(0);
}

/* Enable or disable input devices, based on chipset state and tablet mode */
__override void lid_angle_peripheral_enable(int enable)
{
	/* If the lid is in 360 position, ignore the lid angle,
	 * which might be faulty. Disable keyboard.
	 */
	if (tablet_get_mode() || chipset_in_state(CHIPSET_STATE_ANY_OFF))
		enable = 0;
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	/* Enable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 1);

	/* Enable Trackpad */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 0);

	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 0);

	/* Disable Trackpad */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 1);

	hook_call_deferred(&enable_input_devices_data, 0);
	/* FIXME(dhendrix): Drive USB_PD_RST_ODL low to prevent
	   leakage? (see comment in schematic) */
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* FIXME(dhendrix): Add CHIPSET_RESUME and CHIPSET_SUSPEND
   hooks to enable/disable sensors? */
/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*
 * FIXME(dhendrix): Weak symbol hack until we can get a better solution for
 * both Amenia and Coral.
 */
void chipset_do_shutdown(void)
{
	/* Disable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 0);

	/*Disable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 0);
	while (gpio_get_level(GPIO_PP3300_PG))
		;

	/*Disable 5V rail */
	gpio_set_level(GPIO_EN_PP5000, 0);
	while (gpio_get_level(GPIO_PP5000_PG))
		;
}

void board_hibernate_late(void)
{
	int i;
	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs in hibernate */
		{ GPIO_BAT_LED_BLUE, GPIO_INPUT | GPIO_PULL_UP },
		{ GPIO_BAT_LED_AMBER, GPIO_INPUT | GPIO_PULL_UP },
		{ GPIO_LID_OPEN, GPIO_INT_RISING | GPIO_PULL_DOWN },

		/*
		 * BD99956 handles charge input automatically. We'll disable
		 * charge output in hibernate. Charger will assert ACOK_OD
		 * when VBUS or VCC are plugged in.
		 */
		{ GPIO_USB_C0_5V_EN, GPIO_INPUT | GPIO_PULL_DOWN },
		{ GPIO_USB_C1_5V_EN, GPIO_INPUT | GPIO_PULL_DOWN },
	};

	/* Change GPIOs' state in hibernate for better power consumption */
	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);

	gpio_config_module(MODULE_KEYBOARD_SCAN, 0);

	/*
	 * Calling gpio_config_module sets disabled alternate function pins to
	 * GPIO_INPUT.  But to prevent keypresses causing leakage currents
	 * while hibernating we want to enable GPIO_PULL_UP as well.
	 */
	gpio_set_flags_by_mask(0x2, 0x03, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x1, 0x7F, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x0, 0xE0, GPIO_INPUT | GPIO_PULL_UP);
	/* KBD_KSO2 needs to have a pull-down enabled instead of pull-up */
	gpio_set_flags_by_mask(0x1, 0x80, GPIO_INPUT | GPIO_PULL_DOWN);
}

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
				       { FLOAT_TO_FP(1), 0, 0 },
				       { 0, 0, FLOAT_TO_FP(1) } };

const mat33_fp_t mag_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(1), 0 },
				      { 0, 0, FLOAT_TO_FP(-1) } };

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;

/* FIXME(dhendrix): Copied from Amenia, probably need to tweak for Coral */
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
	 .port = I2C_PORT_LID_ACCEL,
	 .i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 2, /* g, enough for laptop. */
	 .min_frequency = KX022_ACCEL_MIN_FREQ,
	 .max_frequency = KX022_ACCEL_MAX_FREQ,
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
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for lid angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

void board_hibernate(void)
{
	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 */
	chipset_do_shutdown();

	/* Added delay to allow AP to settle down */
	msleep(100);

	/* Enable both the VBUS & VCC ports before entering PG3 */
	bd9995x_select_input_port(BD9995X_CHARGE_PORT_BOTH, 1);

	/* Turn BGATE OFF for saving the power */
	bd9995x_set_power_save_mode(BD9995X_PWR_SAVE_MAX);
}

static void board_set_motion_sensor_count(uint8_t sku_id)
{
	/*
	 * There are two possible sensor configurations. Clamshell device will
	 * not have any of the motion sensors populated, while convertible
	 * devices have the BMI160 Accel/Gryo and Kionx KX022 lid acceleration
	 * sensor. If a new SKU id is used that is not in the table, then the
	 * number of motion sensors will remain as ARRAY_SIZE(motion_sensors).
	 */
	motion_sensor_count =
		SKU_IS_CONVERTIBLE(sku_id) ? ARRAY_SIZE(motion_sensors) : 0;

	CPRINTS("Motion Sensor Count = %d", motion_sensor_count);
}

struct {
	enum coral_board_version version;
	int thresh_mv;
} const coral_board_versions[] = {
	/* Vin = 3.3V, Ideal voltage, R2 values listed below */
	/* R1 = 51.1 kOhm */
	{ BOARD_VERSION_1, 200 }, /* 124 mV, 2.0 Kohm */
	{ BOARD_VERSION_2, 366 }, /* 278 mV, 4.7 Kohm */
	{ BOARD_VERSION_3, 550 }, /* 456 mV, 8.2  Kohm */
	{ BOARD_VERSION_4, 752 }, /* 644 mV, 12.4 Kohm */
	{ BOARD_VERSION_5, 927 }, /* 860 mV, 18.0 Kohm */
	{ BOARD_VERSION_6, 1073 }, /* 993 mV, 22.0 Kohm */
	{ BOARD_VERSION_7, 1235 }, /* 1152 mV, 27.4 Kohm */
	{ BOARD_VERSION_8, 1386 }, /* 1318 mV, 34.0 Kohm */
	{ BOARD_VERSION_9, 1552 }, /* 1453 mV, 40.2 Kohm */
	/* R1 = 10.0 kOhm */
	{ BOARD_VERSION_10, 1739 }, /* 1650 mV, 10.0 Kohm */
	{ BOARD_VERSION_11, 1976 }, /* 1827 mV, 12.4 Kohm */
	{ BOARD_VERSION_12, 2197 }, /* 2121 mV, 18.0 Kohm */
	{ BOARD_VERSION_13, 2344 }, /* 2269 mV, 22.0 Kohm */
	{ BOARD_VERSION_14, 2484 }, /* 2418 mV, 27.4 Kohm */
	{ BOARD_VERSION_15, 2636 }, /* 2550 mV, 34.0 Kohm */
	{ BOARD_VERSION_16, 2823 }, /* 2721 mV, 47.0 Kohm */
};
BUILD_ASSERT(ARRAY_SIZE(coral_board_versions) == BOARD_VERSION_COUNT);

static int board_read_version(enum adc_channel chan)
{
	int mv;
	int i;

	/* ID/SKU enable is active high */
	gpio_set_flags(GPIO_EC_BRD_ID_EN, GPIO_OUT_HIGH);
	/* Wait to allow cap charge */
	msleep(1);
	mv = adc_read_channel(chan);
	CPRINTS("ID/SKU ADC %d = %d mV", chan, mv);
	/* Disable ID/SKU circuit */
	gpio_set_flags(GPIO_EC_BRD_ID_EN, GPIO_INPUT);

	if (mv == ADC_READ_ERROR)
		return BOARD_VERSION_UNKNOWN;

	for (i = 0; i < BOARD_VERSION_COUNT; i++)
		if (mv < coral_board_versions[i].thresh_mv)
			return coral_board_versions[i].version;

	return BOARD_VERSION_UNKNOWN;
}

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	version = board_read_version(ADC_BOARD_ID);

	CPRINTS("Board version: %d", version);
	return version;
}

static void sku_id_init(void)
{
	int sku_id_lower;
	int sku_id_higher;

	if (sku_id == BOARD_VERSION_UNKNOWN) {
		sku_id_lower = board_read_version(ADC_BOARD_SKU_0);
		sku_id_higher = board_read_version(ADC_BOARD_SKU_1);
		if ((sku_id_lower != BOARD_VERSION_UNKNOWN) &&
		    (sku_id_higher != BOARD_VERSION_UNKNOWN))
			sku_id = (sku_id_higher << 4) | sku_id_lower;
		CPRINTS("SKU ID: %d", sku_id);
		/* Use sku_id to set motion sensor count */
		board_set_motion_sensor_count(sku_id);

		if (0 == SKU_IS_CONVERTIBLE(sku_id)) {
			CPRINTS("Disable tablet mode interrupt");
			gpio_disable_interrupt(GPIO_TABLET_MODE_L);
			/* Enfore device in laptop mode */
			tablet_set_mode(0, TABLET_TRIGGER_LID);
		}
	}
}
/* This can't run until after the ADC module has been initialized */
DECLARE_HOOK(HOOK_INIT, sku_id_init, HOOK_PRIO_INIT_ADC + 1);

static void print_form_factor_list(int low, int high)
{
	int id;
	int count = 0;

	if (high > 255)
		high = 255;
	for (id = low; id <= high; id++) {
		ccprintf("SKU ID %03d: %s\n", id,
			 SKU_IS_CONVERTIBLE(id) ? "Convertible" : "Clamshell");
		/* Don't print too many lines at once */
		if (!(++count % 5))
			msleep(20);
	}
}

static int command_sku(int argc, const char **argv)
{
	enum adc_channel chan;

	if (argc < 2) {
		system_get_sku_id();
		ccprintf("SKU ID: %d\n", sku_id);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "form")) {
		if (argc >= 4) {
			char *e;
			int low, high;

			low = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM1;

			high = strtoi(argv[3], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
			print_form_factor_list(low, high);
			return EC_SUCCESS;
		} else {
			return EC_ERROR_PARAM_COUNT;
		}
	}

	if (!strcasecmp(argv[1], "board"))
		chan = ADC_BOARD_ID;
	else if (!strcasecmp(argv[1], "line0"))
		chan = ADC_BOARD_SKU_0;
	else if (!strcasecmp(argv[1], "line1"))
		chan = ADC_BOARD_SKU_1;
	else
		return EC_ERROR_PARAM1;

	ccprintf("sku: %s = %d, adc %d\n", argv[1], board_read_version(chan),
		 chan);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sku, command_sku, "<board|line0|line1|form [low high]>",
			"Get board id, sku, form factor");

__override uint32_t board_get_sku_id(void)
{
	if (sku_id == BOARD_VERSION_UNKNOWN)
		sku_id_init();

	return (uint32_t)sku_id;
}

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us from 50us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	uint32_t sku = system_get_sku_id();

	/*
	 * We always compile in backlight support for coral, but only some
	 * models come with the hardware. Therefore, check if the current
	 * device is one of them and return the default value - with backlight
	 * here.
	 */
	if (sku == 8 || sku == 11)
		return flags0;

	// Report that there is no keyboard backlight
	flags0 &= ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB);

	return flags0;
}
