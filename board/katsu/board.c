/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "charger_mt6370.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/charger/rt946x.h"
#include "driver/sync.h"
#include "driver/tcpm/mt6370.h"
#include "driver/usb_mux/it5205.h"
#include "extpower.h"
#include "gesture.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_policy.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0 /* port */);
}

static void gauge_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_CHARGER);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = { "BOARD_ID", 3300, 4096, 0, STM32_AIN(10) },
	[ADC_EC_SKU_ID] = { "EC_SKU_ID", 3300, 4096, 0, STM32_AIN(8) },
	[ADC_BATT_ID] = { "BATT_ID", 3300, 4096, 0, STM32_AIN(7) },
	[ADC_POGO_ADC_INT_L] = { "POGO_ADC_INT_L", 3300, 4096, 0,
				 STM32_AIN(6) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "typec",
	  .port = 0,
	  .kbps = 400,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "other",
	  .port = 1,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L" },
	{ GPIO_PMIC_EC_RESETB, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = MT6370_TCPC_I2C_ADDR_FLAGS,
		},
		.drv = &mt6370_tcpm_drv,
	},
};

struct mt6370_thermal_bound thermal_bound = {
	.target = 80,
	.err = 4,
};

static void board_hpd_update(const struct usb_mux *me, mux_state_t mux_state,
			     bool *ack_required)
{
	/* This driver does not use host command ACKs */
	*ack_required = false;

	/*
	 * svdm_dp_attention() did most of the work, we only need to notify
	 * host here.
	 */
	host_set_single_event(EC_HOST_EVENT_USB_MUX);
}

__override const struct rt946x_init_setting *board_rt946x_init_setting(void)
{
	static const struct rt946x_init_setting battery_init_setting = {
		.eoc_current = 140,
		.mivr = 4000,
		.ircmp_vclamp = 32,
		.ircmp_res = 25,
		.boost_voltage = 5050,
		.boost_current = 1500,
	};

	return &battery_init_setting;
}

struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
				.hpd_update = &board_hpd_update,
			},
	},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	return status;
}

static int force_discharge;

int board_set_active_charge_port(int charge_port)
{
	CPRINTS("New chg p%d", charge_port);

	/* ignore all request when discharge mode is on */
	if (force_discharge && charge_port != CHARGE_PORT_NONE)
		return EC_SUCCESS;

	switch (charge_port) {
	case CHARGE_PORT_USB_C:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;
		gpio_set_level(GPIO_EN_POGO_CHARGE_L, 1);
		gpio_set_level(GPIO_EN_USBC_CHARGE_L, 0);
		break;
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	case CHARGE_PORT_POGO:
		gpio_set_level(GPIO_EN_USBC_CHARGE_L, 1);
		gpio_set_level(GPIO_EN_POGO_CHARGE_L, 0);
		break;
#endif
	default:
		/*
		 * To ensure the fuel gauge (max17055) is always powered
		 * even when battery is disconnected, keep VBAT rail on but
		 * set the charging current to minimum.
		 */
		gpio_set_level(GPIO_EN_POGO_CHARGE_L, 1);
		gpio_set_level(GPIO_EN_USBC_CHARGE_L, 1);
		charger_set_current(CHARGER_SOLO, 0);
		break;
	}

	return EC_SUCCESS;
}

int board_discharge_on_ac(int enable)
{
	int ret, port;

	if (enable) {
		port = CHARGE_PORT_NONE;
	} else {
		/* restore the charge port state */
		port = charge_manager_get_override();
		if (port == OVERRIDE_OFF)
			port = charge_manager_get_active_charge_port();
	}

	ret = charger_discharge_on_ac(enable);
	if (ret)
		return ret;

	if (force_discharge && !enable)
		rt946x_toggle_bc12_detection();

	force_discharge = enable;
	return board_set_active_charge_port(port);
}

int extpower_is_present(void)
{
	/*
	 * The charger will indicate VBUS presence if we're sourcing 5V,
	 * so exclude such ports.
	 */
	int usb_c_extpower_present;
	static int prev_usb_c_extpower_present;

	if (board_vbus_source_enabled(CHARGE_PORT_USB_C))
		usb_c_extpower_present = 0;
	else
		usb_c_extpower_present =
			tcpm_check_vbus_level(CHARGE_PORT_USB_C, VBUS_PRESENT);

	if (prev_usb_c_extpower_present != usb_c_extpower_present) {
		if (usb_c_extpower_present)
			host_set_single_event(EC_HOST_EVENT_AC_CONNECTED);
		else
			host_set_single_event(EC_HOST_EVENT_AC_DISCONNECTED);
		prev_usb_c_extpower_present = usb_c_extpower_present;
	}

	return usb_c_extpower_present;
}

int pd_snk_is_vbus_provided(int port)
{
	if (port)
		panic("Invalid charge port\n");

	return rt946x_is_vbus_ready();
}

static void board_init(void)
{
	/* If the reset cause is external, pulse PMIC force reset. */
	if (system_get_reset_flags() == EC_RESET_FLAG_RESET_PIN) {
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 0);
		crec_msleep(100);
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 1);
	}

	/* Enable TCPC alert interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_CHARGER_INT_ODL);

#ifdef SECTION_IS_RW
	/* Enable interrupts from ICM40608 sensor. */
	gpio_enable_interrupt(GPIO_ACCEL_INT_ODL);

	/* Enable interrupt for the camera vsync. */
	gpio_enable_interrupt(GPIO_SYNC_INT);
#endif /* SECTION_IS_RW */

	/* Enable interrupt from PMIC. */
	gpio_enable_interrupt(GPIO_PMIC_EC_RESETB);

	/* Enable gauge interrupt from max17055 */
	gpio_enable_interrupt(GPIO_GAUGE_INT_ODL);

	/*
	 * Fix backlight led maximum current:
	 * tolerance 120mA * 0.75 = 90mA.
	 * (b/133655155)
	 */
	mt6370_backlight_set_dim(MT6370_BLDIM_DEFAULT * 3 / 4);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_rev_init(void)
{
	/* Board revision specific configs. */

	/*
	 * It's a P1 pin BOOTBLOCK_MUX_OE, also a P2 pin BC12_DET_EN.
	 * Keep this pin defaults to P1 setting since that eMMC enabled with
	 * High-Z stat.
	 */
	/* TODO */
	/* Put initial code here for different EC board reversion */
}
DECLARE_HOOK(HOOK_INIT, board_rev_init, HOOK_PRIO_INIT_ADC + 1);

void sensor_board_proc_double_tap(void)
{
	CPRINTS("Detect double tap");
}

/* Motion sensors */
/* Mutexes */
#ifndef VARIANT_KUKUI_NO_SENSORS
static struct mutex g_lid_mutex;

static struct icm_drv_data_t g_icm426xx_data;

/* Matrix to rotate accelerometer into standard reference frame */
static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					     { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: ICM40608: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[LID_ACCEL] = {
	 .name = "Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_ICM426XX,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &icm426xx_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_icm426xx_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	 .max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	 .config = {
		 /* Enable accel in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = TAP_ODR,
			 .ec_rate = 100 * MSEC,
		 },
		 /* For double tap detection */
		 [SENSOR_CONFIG_EC_S3] = {
			 .odr = TAP_ODR,
			 .ec_rate = 100 * MSEC,
		 },
	 },
	},
	[LID_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_ICM426XX,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &icm426xx_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_icm426xx_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &lid_standard_ref,
	 .min_frequency = ICM426XX_GYRO_MIN_FREQ,
	 .max_frequency = ICM426XX_GYRO_MAX_FREQ,
	},
	[VSYNC] = {
	 .name = "Camera vsync",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_GPIO,
	 .type = MOTIONSENSE_TYPE_SYNC,
	 .location = MOTIONSENSE_LOC_CAMERA,
	 .drv = &sync_drv,
	 .default_range = 0,
	 .min_frequency = 0,
	 .max_frequency = 1,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
#endif /* VARIANT_KUKUI_NO_SENSORS */

/*
 * Return if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage;
	/*
	 * Though we have a more tolerant range (3.9V~13.4V), setting 4400 to
	 * prevent from a bad charger crashed.
	 *
	 * TODO(b:131284131): mt6370 VBUS reading is not accurate currently.
	 * Vendor will provide a workaround solution to fix the gap between ADC
	 * reading and actual voltage.  After the workaround applied, we could
	 * try to raise this value to 4600.  (when it says it read 4400, it is
	 * actually close to 4600)
	 */
	if (charger_get_vbus_voltage(port, &voltage))
		voltage = 0;

	return voltage < 4400;
}

__override int board_charge_port_is_sink(int port)
{
	/* TODO(b:128386458): Check POGO_ADC_INT_L */
	return 1;
}

__override int board_charge_port_is_connected(int port)
{
	return gpio_get_level(GPIO_POGO_VBUS_PRESENT);
}

__override void
board_fill_source_power_info(int port, struct ec_response_usb_pd_power_info *r)
{
	r->meas.voltage_now = 3300;
	r->meas.voltage_max = 3300;
	r->meas.current_max = 1500;
	r->meas.current_lim = 1500;
	r->max_power = r->meas.voltage_now * r->meas.current_max;
}

/* b/207456334: bugged reserved bits causes device not charging */
static void mt6370_reg_fix(void)
{
	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags, RT946X_REG_CHGCTRL1,
		    BIT(3) | BIT(5), MASK_CLR);
	i2c_update8(chg_chips[CHARGER_SOLO].i2c_port,
		    chg_chips[CHARGER_SOLO].i2c_addr_flags, RT946X_REG_CHGCTRL2,
		    BIT(5) | BIT(RT946X_SHIFT_BATDET_DIS_DLY), MASK_CLR);
}
DECLARE_HOOK(HOOK_INIT, mt6370_reg_fix, HOOK_PRIO_DEFAULT);
