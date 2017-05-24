/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Poppy board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "als.h"
#include "bd99992gw.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_ramp.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/als_opt3001.h"
#include "driver/baro_bmp280.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8751.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/temp_sensor/bd99992gw.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
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
#include "espi.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C0_PD_INT_ODL) &&
	    !gpio_get_level(GPIO_USB_C0_PD_RST_L))
		return;
	else if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
		 !gpio_get_level(GPIO_USB_C1_PD_RST_L))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(0, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	usb_charger_vbus_change(1, !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
}

void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
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
		task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET, 0);
}
DECLARE_DEFERRED(anx74xx_cable_det_handler);

void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* debounce for 2 msec */
	hook_call_deferred(&anx74xx_cable_det_handler_data, (2 * MSEC));
}
#endif

/*
 * Base detection and debouncing
 *
 * TODO(b/35585396): Fine-tune these values.
 */
#define BASE_DETECT_DEBOUNCE_US (5 * MSEC)

/*
 * rev0: Lid has 100K pull-up, base has 5.1K pull-down, so the ADC
 * value should be around 5.1/(100+5.1)*3300 = 160.
 * >=rev1: Lid has 604K pull-up, base has 30.1K pull-down, so the
 * ADC value should be around 30.1/(604+30.1)*3300 = 156
 */
#define BASE_DETECT_MIN_MV 140
#define BASE_DETECT_MAX_MV 200

/*
 * Base EC pulses detection pin for 100 us to signal out of band USB wake (that
 * can be used to wake system from deep S3).
 */
#define BASE_DETECT_PULSE_MIN_US 90
#define BASE_DETECT_PULSE_MAX_US 120

static uint64_t base_detect_debounce_time;

static void base_detect_deferred(void);
DECLARE_DEFERRED(base_detect_deferred);

enum base_status {
	BASE_UNKNOWN = 0,
	BASE_DISCONNECTED = 1,
	BASE_CONNECTED = 2,
};

static enum base_status current_base_status;

/*
 * This function is called whenever there is a change in the base detect
 * status. Actions taken include:
 * 1. Change in power to base
 * 2. Indicate mode change to host.
 * 3. Indicate tablet mode to host. Current assumption is that if base is
 * disconnected then the system is in tablet mode, else if the base is
 * connected, then the system is not in tablet mode.
 */
static void base_detect_change(enum base_status status)
{
	int connected = (status == BASE_CONNECTED);

	CPRINTS("Base %sconnected", connected ? "" : "not ");
	gpio_set_level(GPIO_PP3300_DX_BASE, connected);
	host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
	tablet_set_mode(!connected);
	current_base_status = status;
}

/* Measure detection pin pulse duration (used to wake AP from deep S3). */
static uint64_t pulse_start;
static uint32_t pulse_width;

static void base_detect_deferred(void)
{
	uint64_t time_now = get_time().val;
	int v;
	uint32_t tmp_pulse_width = pulse_width;

	if (base_detect_debounce_time > time_now) {
		hook_call_deferred(&base_detect_deferred_data,
				   base_detect_debounce_time - time_now);
		return;
	}

	v = adc_read_channel(ADC_BASE_DET);
	if (v == ADC_READ_ERROR)
		return;
	CPRINTS("%s = %d (pulse %d)", adc_channels[ADC_BASE_DET].name,
		v, tmp_pulse_width);

	if (v >= BASE_DETECT_MIN_MV && v <= BASE_DETECT_MAX_MV) {
		if (current_base_status != BASE_CONNECTED) {
			base_detect_change(BASE_CONNECTED);
		} else if (tmp_pulse_width >= BASE_DETECT_PULSE_MIN_US &&
			   tmp_pulse_width <= BASE_DETECT_PULSE_MAX_US) {
			CPRINTS("Sending event to AP");
			host_set_single_event(EC_HOST_EVENT_KEY_PRESSED);
		}
	} else {
		/*
		 * TODO(b/35585396): Figure out what to do with
		 * other ADC values that do not clearly indicate base
		 * presence or absence.
		 */
		if (current_base_status != BASE_DISCONNECTED)
			base_detect_change(BASE_DISCONNECTED);
	}
}

static inline int detect_pin_connected(enum gpio_signal det_pin)
{
	return gpio_get_level(det_pin) == 0;
}

void base_detect_interrupt(enum gpio_signal signal)
{
	uint64_t time_now = get_time().val;

	if (base_detect_debounce_time <= time_now) {
		/*
		 * Detect and measure detection pin pulse, when base is
		 * connected. Only a single pulse is measured over a debounce
		 * period. If no pulse, or multiple pulses are detected,
		 * pulse_width is set to 0.
		 */
		if (current_base_status == BASE_CONNECTED &&
		    !detect_pin_connected(signal)) {
			pulse_start = time_now;
		} else {
			pulse_start = 0;
		}
		pulse_width = 0;

		hook_call_deferred(&base_detect_deferred_data,
				   BASE_DETECT_DEBOUNCE_US);
	} else {
		if (current_base_status == BASE_CONNECTED &&
		    detect_pin_connected(signal) && !pulse_width &&
		    pulse_start) {
			/* First pulse within period. */
			pulse_width = time_now - pulse_start;
		} else {
			pulse_start = 0;
			pulse_width = 0;
		}
	}

	base_detect_debounce_time = time_now + BASE_DETECT_DEBOUNCE_US;
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_ESPI_VW_SIGNALS
	{VW_SLP_S3_L,		1, "SLP_S3_DEASSERTED"},
	{VW_SLP_S4_L,		1, "SLP_S4_DEASSERTED"},
#else
	{GPIO_PCH_SLP_S3_L,	1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,	1, "SLP_S4_DEASSERTED"},
#endif
	{GPIO_PCH_SLP_SUS_L,	1, "SLP_SUS_DEASSERTED"},
	{GPIO_RSMRST_L_PGOOD,	1, "RSMRST_L_PGOOD"},
	{GPIO_PMIC_DPWROK,	1, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Hibernate wake configuration */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Base detection */
	[ADC_BASE_DET] = {"BASE_DET", NPCX_ADC_CH0,
			  ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	/* Vbus sensing (10x voltage divider). */
	[ADC_VBUS] = {"VBUS", NPCX_ADC_CH2, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = {"AMON_BMON", NPCX_ADC_CH1, ADC_MAX_VOLT*1000/18,
			   ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[]  = {
	{"tcpc",      NPCX_I2C_PORT0_0, 400, GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"als",       NPCX_I2C_PORT0_1, 400, GPIO_I2C0_1_SCL, GPIO_I2C0_1_SDA},
	{"charger",   NPCX_I2C_PORT1,   100, GPIO_I2C1_SCL,   GPIO_I2C1_SDA},
	{"pmic",      NPCX_I2C_PORT2,   400, GPIO_I2C2_SCL,   GPIO_I2C2_SDA},
	{"accelgyro", NPCX_I2C_PORT3,   400, GPIO_I2C3_SCL,   GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{NPCX_I2C_PORT0_0, 0x50, &anx74xx_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
	{NPCX_I2C_PORT0_0, 0x16, &tcpci_tcpm_drv, TCPC_ALERT_ACTIVE_LOW},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0, /* don't care / unused */
		.driver = &anx74xx_tcpm_usb_mux_driver,
		.hpd_update = &anx74xx_tcpc_update_hpd_status,
	},
	{
		.port_addr = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8751_tcpc_update_hpd_status,
	}
};

struct mutex pericom_mux_lock;
struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_0,
		.mux_lock = &pericom_mux_lock,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = &pericom_mux_lock,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

/* called from anx74xx_set_power_mode() */
void board_set_tcpc_power_mode(int port, int mode)
{
	if (port != 0)
		return;

	if (mode) {
		gpio_set_level(GPIO_USB_C0_TCPC_PWR, 1);
		msleep(10);
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
	} else {
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
		msleep(1);
		gpio_set_level(GPIO_USB_C0_TCPC_PWR, 0);
	}
}

void board_reset_pd_mcu(void)
{
	/* Assert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);
	msleep(1);
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
	/* Disable power */
	gpio_set_level(GPIO_USB_C0_TCPC_PWR, 0);
	msleep(10);
	/* Enable power */
	gpio_set_level(GPIO_USB_C0_TCPC_PWR, 1);
	msleep(10);
	/* Deassert reset */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
}

void board_tcpc_init(void)
{
	int port, reg;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image()) {
		gpio_set_level(GPIO_PP3300_USB_PD, 1);
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		msleep(10);
		board_reset_pd_mcu();
	}

	/*
	 * TODO: Remove when Poppy is updated with PS8751 A3.
	 *
	 * Force PS8751 A2 to wake from low power mode.
	 * If PS8751 remains in low power mode after sysjump,
	 * TCPM_INIT will fail due to not able to access PS8751.
	 *
	 * NOTE: PS8751 A3 will wake on any I2C access.
	 */
	i2c_read8(NPCX_I2C_PORT0_1, 0x10, 0xA0, &reg);

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);
#endif

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C+1);

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
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0, 4},

	/* These BD99992GW temp sensors are only readable in S0 */
	{"Ambient", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	 BD99992GW_ADC_CHANNEL_SYSTHERM0, 4},
	{"Charger", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	 BD99992GW_ADC_CHANNEL_SYSTHERM1, 4},
	{"DRAM", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	 BD99992GW_ADC_CHANNEL_SYSTHERM2, 4},
	{"eMMC", TEMP_SENSOR_TYPE_BOARD, bd99992gw_get_val,
	 BD99992GW_ADC_CHANNEL_SYSTHERM3, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	/* TODO(crosbug.com/p/61098): verify attenuation_factor */
	{"TI", opt3001_init, opt3001_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	[BUTTON_VOLUME_DOWN] = {"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN,
				GPIO_VOLUME_DOWN_L, 30 * MSEC, 0},
	[BUTTON_VOLUME_UP] = {"Volume Up", KEYBOARD_BUTTON_VOLUME_UP,
			      GPIO_VOLUME_UP_L, 30 * MSEC, 0},
};

const struct button_config *recovery_buttons[] = {
	&buttons[BUTTON_VOLUME_DOWN],
	&buttons[BUTTON_VOLUME_UP],
};
const int recovery_buttons_count = ARRAY_SIZE(recovery_buttons);

static void board_pmic_init(void)
{
	if (system_jumped_to_this_image())
		return;

	/* DISCHGCNT3 - enable 100 ohm discharge on V1.00A */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x3e, 0x04);

	/* Set CSDECAYEN / VCCIO decays to 0V at assertion of SLP_S0# */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x30, 0x4a);

	/*
	 * Set V100ACNT / V1.00A Control Register:
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x37, 0x1a);

	/*
	 * Set V085ACNT / V0.85A Control Register:
	 * Lower power mode = 0.7V.
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x38, 0x7a);

	/* VRMODECTRL - disable low-power mode for all rails */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x3b, 0x1f);
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	/* Provide AC status to the PCH */
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());

	/* Enable sensors power supply */
	gpio_set_level(GPIO_PP1800_DX_SENSOR, 1);
	gpio_set_level(GPIO_PP3300_DX_SENSOR, 1);

	/* Enable VBUS interrupt */
	if (system_get_board_version() == 0) {
		/*
		 * crosbug.com/p/61929: rev0 does not have VBUS detection,
		 * force detection on both ports.
		 */
		gpio_set_flags(GPIO_USB_C0_VBUS_WAKE_L,
			GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_USB_C1_VBUS_WAKE_L,
			GPIO_INPUT | GPIO_PULL_DOWN);

		vbus0_evt(GPIO_USB_C0_VBUS_WAKE_L);
		vbus1_evt(GPIO_USB_C1_VBUS_WAKE_L);
	} else {
		gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
		gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);
	}

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);

	/* Enable base detection interrupt */
	base_detect_debounce_time = get_time().val;
	hook_call_deferred(&base_detect_deferred_data, 0);
	gpio_enable_interrupt(GPIO_BASE_DET_A);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Buffer the AC present GPIO to the PCH.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACOK, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

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
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source VBUS on the port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTF("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTF("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable both ports */
		gpio_set_level(GPIO_USB_C0_CHARGE_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_L, 1);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_L :
					     GPIO_USB_C1_CHARGE_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_L :
					     GPIO_USB_C0_CHARGE_L, 0);
	}

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
				   CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

/**
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected */
	if (system_get_image_copy() != SYSTEM_IMAGE_RW
		&& system_is_locked())
		return 0;
	else
		return (supplier == CHARGE_SUPPLIER_BC12_DCP ||
			supplier == CHARGE_SUPPLIER_BC12_SDP ||
			supplier == CHARGE_SUPPLIER_BC12_CDP ||
			supplier == CHARGE_SUPPLIER_OTHER);
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

/**
 * Return if board is consuming full amount of input current
 */
int board_is_consuming_full_charge(void)
{
	int chg_perc = charge_get_percent();

	return chg_perc > 2 && chg_perc < 95;
}


void board_hibernate(void)
{
    CPRINTS("Triggering PMIC shutdown.");
    uart_flush_output();

    /* Trigger PMIC shutdown. */
    if (i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x49, 0x01)) {
        /*
         * If we can't tell the PMIC to shutdown, instead reset
         * and don't start the AP. Hopefully we'll be able to
         * communicate with the PMIC next time.
         */
        CPRINTS("PMIC i2c failed.");
        system_reset(SYSTEM_RESET_LEAVE_AP_OFF);
    }

    /* Await shutdown. */
    while (1)
        ;
}

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

struct bmi160_drv_data_t g_bmi160_data;
struct bmp280_drv_data_t bmp280_drv_data;

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t mag_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0,  FLOAT_TO_FP(1), 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

const matrix_3x3_t lid_standard_ref = {
	{ 0,  FLOAT_TO_FP(1),  0},
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0,  0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2,  /* g, enough for laptop. */
	 .min_frequency = BMI160_ACCEL_MIN_FREQ,
	 .max_frequency = BMI160_ACCEL_MAX_FREQ,
	 .config = {
		 /* AP: by default use EC settings */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0
		 },
	 },
	},

	[LID_GYRO] = {
	 .name = "Lid Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &lid_standard_ref,
	 .min_frequency = BMI160_GYRO_MIN_FREQ,
	 .max_frequency = BMI160_GYRO_MAX_FREQ,
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC does not need in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		 },
	 },
	},

	[LID_MAG] = {
	 .name = "Lid Mag",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_MAG,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .default_range = 1 << 11, /* 16LSB / uT, fixed */
	 .rot_standard_ref = &mag_standard_ref,
	 .min_frequency = BMM150_MAG_MIN_FREQ,
	 .max_frequency = BMM150_MAG_MAX_FREQ,
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC does not need in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			 .odr = 0,
			 .ec_rate = 0,
		 },
	 },
	},

	[LID_BARO] = {
	 .name = "Base Baro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMP280,
	 .type = MOTIONSENSE_TYPE_BARO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmp280_drv,
	 .drv_data = &bmp280_drv_data,
	 .port = I2C_PORT_BARO,
	 .addr = BMP280_I2C_ADDRESS1,
	 .default_range = 1 << 18, /*  1bit = 4 Pa, 16bit ~= 2600 hPa */
	 .min_frequency = BMP280_BARO_MIN_FREQ,
	 .max_frequency = BMP280_BARO_MAX_FREQ,
	 .config = {
		/* AP: by default shutdown all sensors */
		[SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		},
		/* EC does not need in S0 */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		},
		/* Sensor off in S3/S5 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		},
		/* Sensor off in S3/S5 */
		[SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		},
	 },
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

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
