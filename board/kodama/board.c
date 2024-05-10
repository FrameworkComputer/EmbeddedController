/* Copyright 2019 The ChromiumOS Authors
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
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/charger/rt946x.h"
#include "driver/sync.h"
#include "driver/tcpm/mt6370.h"
#include "driver/usb_mux/it5205.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_bitbang.h"
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
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0 /* port */);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = { "BOARD_ID", 3300, 4096, 0, STM32_AIN(10) },
	[ADC_EC_SKU_ID] = { "EC_SKU_ID", 3300, 4096, 0, STM32_AIN(8) },
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
	  .sda = GPIO_I2C2_SDA,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct i2c_port_t i2c_bitbang_ports[] = {
	{ .name = "battery",
	  .port = 2,
	  .kbps = 100,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA,
	  .drv = &bitbang_drv },
};
const unsigned int i2c_bitbang_ports_used = ARRAY_SIZE(i2c_bitbang_ports);

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
	.target = 75,
	.err = 4,
};

static void board_hpd_status(const struct usb_mux *me, mux_state_t mux_state,
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
		.eoc_current = 150,
		.mivr = 4000,
		.ircmp_vclamp = 32,
		.ircmp_res = 25,
		.boost_voltage = 5050,
		.boost_current = 1500,
	};

	return &battery_init_setting;
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
				.hpd_update = &board_hpd_status,
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
		break;
	default:
		/*
		 * To ensure the fuel gauge (max17055) is always powered
		 * even when battery is disconnected, keep VBAT rail on but
		 * set the charging current to minimum.
		 */
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

	if (board_vbus_source_enabled(CHARGE_PORT_USB_C))
		usb_c_extpower_present = 0;
	else
		usb_c_extpower_present =
			tcpm_check_vbus_level(CHARGE_PORT_USB_C, VBUS_PRESENT);

	return usb_c_extpower_present;
}

int pd_snk_is_vbus_provided(int port)
{
	if (port)
		panic("Invalid charge port\n");

	return rt946x_is_vbus_ready();
}

#define CHARGER_I2C_ADDR_FLAGS RT946X_ADDR_FLAGS

static void board_init(void)
{
#ifdef SECTION_IS_RW
	int val;

	i2c_read8(I2C_PORT_CHARGER, CHARGER_I2C_ADDR_FLAGS, RT946X_REG_CHGCTRL1,
		  &val);
	val &= RT946X_MASK_OPA_MODE;
	i2c_write8(I2C_PORT_CHARGER, CHARGER_I2C_ADDR_FLAGS,
		   RT946X_REG_CHGCTRL1, (val | RT946X_MASK_STAT_EN));
#endif

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
	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACCEL_INT_ODL);

	/* Enable interrupt for the camera vsync. */
	gpio_enable_interrupt(GPIO_SYNC_INT);
#endif /* SECTION_IS_RW */

	/* Enable interrupt from PMIC. */
	gpio_enable_interrupt(GPIO_PMIC_EC_RESETB);

	/* reduce mt6370 db and bl driving capacity */
	mt6370_reduce_db_bl_driving();

	/* Display bias settings. */
	mt6370_db_set_voltages(6000, 5800, 5800);

	/*
	 * Fix backlight led maximum current:
	 * tolerance 120mA * 0.75 = 90mA.
	 * (b/133655155)
	 */
	mt6370_backlight_set_dim(MT6370_BLDIM_DEFAULT * 3 / 4);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/*
 * Re-configure i2c-2 to 100kHz for EVT devices, this must execute after
 * i2c_init (in main()) and before battery fuel gauge access the battery
 * (i.e. HOOK_PRIO_I2C + 1).
 *
 * Note that stm32f0 don't run adc_init in hooks, so we can safely call
 * board_get_version() before HOOK_PRIO_INIT_ADC(=HOOK_PRIO_DEFAULT).
 */
static void board_i2c_init(void)
{
	if (board_get_version() < 2)
		i2c_set_freq(1, I2C_FREQ_100KHZ);
}
DECLARE_HOOK(HOOK_INIT, board_i2c_init, HOOK_PRIO_INIT_I2C);

/* Motion sensors */
/* Mutexes */
#ifdef SECTION_IS_RW
static struct mutex g_lid_mutex;

static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* Matrix to rotate accelerometer into standard reference frame */
static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					     { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = LSM6DSM_ST_DATA(lsm6dsm_data, MOTIONSENSE_TYPE_ACCEL),
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	 .config = {
		 /* Enable accel in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 13000 | ROUND_UP_FLAG,
			 .ec_rate = 100 * MSEC,
		 },
	 },
	},
	[LID_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = LSM6DSM_ST_DATA(lsm6dsm_data, MOTIONSENSE_TYPE_GYRO),
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 1000 | ROUND_UP_FLAG, /* dps */
	 .rot_standard_ref = &lid_standard_ref,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
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
#endif /* SECTION_IS_RW */

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

int board_get_battery_i2c(void)
{
	return board_get_version() >= 2 ? 2 : 1;
}
