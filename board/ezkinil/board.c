/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "button.h"
#include "cbi_ssfc.h"
#include "charge_state.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm42607.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/retimer/tusb544.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/usb_mux/amd_fp5.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usbc_ppc.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

static int board_ver;

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 }, { 1, 0 },   { 0, 6 },	  { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 }, { 1, 3 },   { -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 }, { 1, 5 },   { 2, 6 },	  { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 }, { 1, 2 },   { 2, 3 },	  { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { 0, 4 }, { -1, -1 }, { 8, 2 },	  { -1, -1 },
	{ -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;
static struct icm_drv_data_t g_icm426xx_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
				       { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, 0, FLOAT_TO_FP(-1) } };
const mat33_fp_t base_standard_ref_1 = { { FLOAT_TO_FP(1), 0, 0 },
					 { 0, FLOAT_TO_FP(-1), 0 },
					 { 0, 0, FLOAT_TO_FP(-1) } };
const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
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
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2, /* g, enough for laptop. */
	 .min_frequency = KX022_ACCEL_MIN_FREQ,
	 .max_frequency = KX022_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		/* EC use accel for angle detection */
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
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t icm426xx_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	.rot_standard_ref = &base_standard_ref_1,
	.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_standard_ref_1,
	.min_frequency = ICM426XX_GYRO_MIN_FREQ,
	.max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

struct motion_sensor_t icm42607_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM42607,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm42607_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
	.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	.rot_standard_ref = &base_standard_ref_1,
	.min_frequency = ICM42607_ACCEL_MIN_FREQ,
	.max_frequency = ICM42607_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm42607_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM42607,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm42607_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_standard_ref_1,
	.min_frequency = ICM42607_GYRO_MIN_FREQ,
	.max_frequency = ICM42607_GYRO_MAX_FREQ,
};

const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_FAN] = {
		.channel = 2,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

const int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_USB_A0_5V,
	IOEX_EN_USB_A1_5V_DB,
};

const struct pi3hdx1204_tuning pi3hdx1204_tuning = {
	.eq_ch0_ch1_offset = PI3HDX1204_EQ_DB710,
	.eq_ch2_ch3_offset = PI3HDX1204_EQ_DB710,
	.vod_offset = PI3HDX1204_VOD_130_ALL_CHANNELS,
	.de_offset = PI3HDX1204_DE_DB_MINUS5,
};

/*
 * USB C0 port SBU mux use standalone FSUSB42UMX
 * chip and it need a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			      bool *ack_required)
{
	/* This driver does not use host command ACKs */
	*ack_required = false;

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 1);
	else
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 0);
	return EC_SUCCESS;
}
/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
const struct usb_mux_driver usbc0_sbu_mux_driver = {
	.set = fsusb42umx_set_mux,
};
/*
 * Since FSUSB42UMX is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux_chain usbc0_sbu_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.driver = &usbc0_sbu_mux_driver,
		},
};

/*****************************************************************************
 * Base Gyro Sensor dynamic configuration
 */

static enum ec_ssfc_base_gyro_sensor base_gyro_config = SSFC_BASE_GYRO_NONE;

static void setup_base_gyro_config(void)
{
	base_gyro_config = ec_config_has_base_gyro_sensor();

	if (base_gyro_config == SSFC_BASE_GYRO_ICM426XX) {
		motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
		motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
		ccprints("BASE GYRO is ICM426XX");
	} else if (base_gyro_config == SSFC_BASE_GYRO_ICM42607) {
		motion_sensors[BASE_ACCEL] = icm42607_base_accel;
		motion_sensors[BASE_GYRO] = icm42607_base_gyro;
		ccprints("BASE GYRO is ICM42607");
	} else if (base_gyro_config == SSFC_BASE_GYRO_BMI160)
		ccprints("BASE GYRO is BMI160");
}

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_gyro_config) {
	case SSFC_BASE_GYRO_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	case SSFC_BASE_GYRO_ICM42607:
		icm42607_interrupt(signal);
		break;
	case SSFC_BASE_GYRO_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
}

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */

/* Place holder for second mux in USBC1 chain */
struct usb_mux_chain usbc1_mux1;

int board_usbc1_retimer_inhpd = IOEX_USB_C1_HPD_IN_DB;

static void setup_mux(void)
{
	enum ec_ssfc_c1_mux mux = get_cbi_ssfc_c1_mux();

	if (mux == SSFC_C1_MUX_NONE && ec_config_has_usbc1_retimer_tusb544())
		mux = SSFC_C1_MUX_TUSB544;

	if (mux == SSFC_C1_MUX_PS8818) {
		ccprints("C1 PS8818 detected");
		/*
		 * Main MUX is FP5, secondary MUX is PS8818
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the AMD FP5
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_amd_fp5_usb_mux;
		/* Set the PS8818 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_ps8818;
	} else if (mux == SSFC_C1_MUX_TUSB544) {
		ccprints("C1 TUSB544 detected");
		/*
		 * Main MUX is FP5, secondary MUX is TUSB544
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the AMD FP5
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_amd_fp5_usb_mux;
		/* Set the TUSB544 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_tusb544;
	} else if (ec_config_has_usbc1_retimer_ps8743()) {
		ccprints("C1 PS8743 detected");
		/*
		 * Main MUX is PS8743, secondary MUX is modified FP5
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the PS8743
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_ps8743;
		/* Set the AMD FP5 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_amd_fp5_usb_mux;
		/* Don't have the AMD FP5 flip */
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;
	}
}

struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_USB_AP_MUX,
			.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
			.driver = &amd_fp5_usb_mux_driver,
		},
		.next = &usbc0_sbu_mux,
	},
	[USBC_PORT_C1] = {
		/* Filled in dynamically at startup */
		.next = &usbc1_mux1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

static int board_tusb544_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	} else {
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);
	}
	return EC_SUCCESS;
}

static int board_ps8743_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

	return EC_SUCCESS;
}

const struct usb_mux usbc1_tusb544 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = TUSB544_I2C_ADDR_FLAGS1,
	.driver = &tusb544_drv,
	.board_set = &board_tusb544_mux_set,
};
const struct usb_mux usbc1_ps8743 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8743_I2C_ADDR1_FLAG,
	.driver = &ps8743_usb_mux_driver,
	.board_set = &board_ps8743_mux_set,
};

/*****************************************************************************
 * PPC
 */

static int ppc_id;

static void setup_c1_ppc_config(void)
{
	/*
	 * Read USB_C1_POWER_SWITCH_ID to choose DB ppc chip
	 * 0: NX20P3483UK
	 * 1: AOZ1380DI
	 */

	ioex_get_level(IOEX_USB_C1_POWER_SWITCH_ID, &ppc_id);

	ccprints("C1: PPC is %s", ppc_id ? "AOZ1380DI" : "NX20P3483UK");

	if (ppc_id) {
		ppc_chips[USBC_PORT_C1].drv = &aoz1380_drv;
		ioex_set_flags(IOEX_USB_C1_PPC_ILIM_3A_EN, GPIO_OUT_LOW);
	}
}

__override void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_FAULT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;
	case GPIO_USB_C1_PPC_INT_ODL:
		if (ppc_id)
			aoz1380_interrupt(USBC_PORT_C1);
		else
			nx20p348x_interrupt(USBC_PORT_C1);
		break;
	default:
		break;
	}
}

__override int
board_aoz1380_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv;

	/* Use the TCPC to set the current limit */
	rv = ioex_set_level(port ? IOEX_USB_C1_PPC_ILIM_3A_EN :
				   IOEX_USB_C0_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);

	return rv;
}

/*****************************************************************************
 * Use FW_CONFIG to set correct configuration.
 */

static void setup_v0_charger(void)
{
	int rv;

	rv = cbi_get_board_version(&board_ver);
	if (rv) {
		ccprints("Fail to get board_ver");
		/* Default for v3 */
		board_ver = 3;
	}

	if (board_ver == 1)
		chg_chips[0].i2c_port = I2C_PORT_CHARGER_V0;
}
/*
 * Use HOOK_PRIO_INIT_I2C so we re-map before charger_chips_init()
 * talks to the charger.
 */
DECLARE_HOOK(HOOK_INIT, setup_v0_charger, HOOK_PRIO_INIT_I2C);

static void setup_fw_config(void)
{
	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);

	setup_mux();

	if (board_ver >= 3)
		setup_c1_ppc_config();

	if (ec_config_has_hdmi_conn_hpd()) {
		if (board_ver < 3)
			ioex_enable_interrupt(IOEX_HDMI_CONN_HPD_3V3_DB);
		else
			gpio_enable_interrupt(GPIO_DP1_HPD_EC_IN);
	}

	setup_base_gyro_config();
}
/* Use HOOK_PRIO_INIT_I2C + 2 to be after ioex_init(). */
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

static int check_hdmi_hpd_status(void)
{
	int hpd = 0;

	if (board_ver < 3)
		ioex_get_level(IOEX_HDMI_CONN_HPD_3V3_DB, &hpd);
	else
		hpd = gpio_get_level(GPIO_DP1_HPD_EC_IN);

	return hpd;
}

static void hdmi_hpd_handler(void)
{
	/* Pass HPD through from DB OPT1 HDMI connector to AP's DP1. */
	int hpd = check_hdmi_hpd_status();

	gpio_set_level(GPIO_DP1_HPD, hpd);
	ccprints("HDMI HPD %d", hpd);
	pi3hdx1204_enable(
		I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) && hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

void hdmi_hpd_interrupt_v2(enum ioex_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

/*****************************************************************************
 * Board suspend / resume
 */

static void board_chipset_resume(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 1);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		ioex_set_level(IOEX_HDMI_POWER_EN_DB, 1);
		crec_msleep(PI3HDX1204_POWER_ON_DELAY_MS);
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
				  check_hdmi_hpd_status());
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 0);
		ioex_set_level(IOEX_HDMI_POWER_EN_DB, 0);
	}

	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * Fan
 */

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3200,
	.rpm_start = 3200,
	.rpm_max = 6000,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

int board_get_temp(int idx, int *temp_k)
{
	int mv;
	int temp_c;
	enum adc_channel channel;

	/* idx is the sensor index set in board temp_sensors[] */
	switch (idx) {
	case TEMP_SENSOR_CHARGER:
		channel = ADC_TEMP_SENSOR_CHARGER;
		break;
	case TEMP_SENSOR_SOC:
		/* thermistor is not powered in G3 */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			return EC_ERROR_NOT_POWERED;

		channel = ADC_TEMP_SENSOR_SOC;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	mv = adc_read_channel(channel);
	if (mv < 0)
		return EC_ERROR_INVAL;

	temp_c = thermistor_linear_interpolate(mv, &thermistor_info);
	*temp_k = C_TO_K(temp_c);
	return EC_SUCCESS;
}

const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_SOC,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_thermistor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
	},
	.temp_fan_off = 0,
	.temp_fan_max = 0,
};

const static struct ec_thermal_config thermal_soc = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(32),
	.temp_fan_max = C_TO_K(75),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

struct fan_step {
	int on;
	int off;
	int rpm;
};

/* Note: Do not make the fan on/off point equal to 0 or 100 */
static const struct fan_step fan_table0[] = {
	{ .on = 0, .off = 1, .rpm = 0 },
	{ .on = 9, .off = 1, .rpm = 3200 },
	{ .on = 21, .off = 7, .rpm = 3500 },
	{ .on = 28, .off = 16, .rpm = 3900 },
	{ .on = 37, .off = 26, .rpm = 4200 },
	{ .on = 47, .off = 35, .rpm = 4600 },
	{ .on = 56, .off = 44, .rpm = 5100 },
	{ .on = 72, .off = 60, .rpm = 5500 },
};
/* All fan tables must have the same number of levels */
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table0)

static const struct fan_step *fan_table = fan_table0;

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_SOC] = thermal_soc;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

int fan_percent_to_rpm(int fan, int pct)
{
	static int current_level;
	static int previous_pct;
	int i;
	/*
	 * Compare the pct and previous pct, we have the three paths :
	 *  1. decreasing path. (check the off point)
	 *  2. increasing path. (check the on point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (pct < previous_pct) {
		for (i = current_level; i >= 0; i--) {
			if (pct <= fan_table[i].off)
				current_level = i - 1;
			else
				break;
		}
	} else if (pct > previous_pct) {
		for (i = current_level + 1; i < NUM_FAN_LEVELS; i++) {
			if (pct >= fan_table[i].on)
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	previous_pct = pct;

	if (fan_table[current_level].rpm != fan_get_rpm_target(FAN_CH(fan))) {
		cprints(CC_THERMAL, "Setting fan RPM to %d",
			fan_table[current_level].rpm);
		board_print_temps();
	}

	return fan_table[current_level].rpm;
}
