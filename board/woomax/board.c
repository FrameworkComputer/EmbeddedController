/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#include "adc.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "cbi_ssfc.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/retimer/pi3dpx1207.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/retimer/ps8802.h"
#include "driver/retimer/ps8811.h"
#include "driver/retimer/ps8818_public.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/usb_mux/amd_fp5.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
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
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_charge.h"
#include "usb_mux.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;
static struct icm_drv_data_t g_icm426xx_data;

/* Rotation matrix for the lid accelerometer */
static const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0 },
	{ 0, FLOAT_TO_FP(-1), 0 },
	{ 0, 0, FLOAT_TO_FP(1) },
};

static const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0 },
	{ FLOAT_TO_FP(-1), 0, 0 },
	{ 0, 0, FLOAT_TO_FP(-1) },
};

static const mat33_fp_t base_standard_ref_icm = {
	{ FLOAT_TO_FP(1), 0, 0 },
	{ 0, FLOAT_TO_FP(-1), 0 },
	{ 0, 0, FLOAT_TO_FP(-1) },
};

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
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
	.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs. */
	.rot_standard_ref = &base_standard_ref_icm,
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
	.rot_standard_ref = &base_standard_ref_icm,
	.min_frequency = ICM426XX_GYRO_MIN_FREQ,
	.max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

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

static void setup_base_gyro_config(void)
{
	if (get_cbi_ssfc_base_sensor() == SSFC_BASE_GYRO_ICM426XX) {
		motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
		motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
		ccprints("BASE GYRO is ICM426XX");
	} else
		ccprints("BASE GYRO is BMI160");
}

void motion_interrupt(enum gpio_signal signal)
{
	if (get_cbi_ssfc_base_sensor() == SSFC_BASE_GYRO_ICM426XX)
		icm426xx_interrupt(signal);
	else
		bmi160_interrupt(signal);
}

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
};

const struct pi3hdx1204_tuning pi3hdx1204_tuning = {
	.eq_ch0_ch1_offset = PI3HDX1204_EQ_DB710,
	.eq_ch2_ch3_offset = PI3HDX1204_EQ_DB710,
	.vod_offset = PI3HDX1204_VOD_130_ALL_CHANNELS,
	.de_offset = PI3HDX1204_DE_DB_MINUS5,
};

/*****************************************************************************
 * Board suspend / resume
 */
#define PS8811_ACCESS_RETRIES 2

static void board_chipset_resume(void)
{
	int rv;
	int retry;
	int hpd = gpio_get_level(GPIO_DP1_HPD_EC_IN);

	ioex_set_level(IOEX_USB_A0_RETIMER_EN, 1);
	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 1);

	/* USB-A0 can run with default settings */
	for (retry = 0; retry < PS8811_ACCESS_RETRIES; ++retry) {
		int val;

		rv = i2c_read8(I2C_PORT_USBA0,
			       PS8811_I2C_ADDR_FLAGS3 + PS8811_REG_PAGE1,
			       PS8811_REG1_USB_BEQ_LEVEL, &val);
		if (!rv)
			break;
	}
	if (rv) {
		ioex_set_level(IOEX_USB_A0_RETIMER_EN, 0);
		CPRINTSUSB("A0: PS8811 not detected");
	}

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		ioex_set_level(IOEX_HDMI_POWER_EN_DB, 1);
		crec_msleep(PI3HDX1204_POWER_ON_DELAY_MS);
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
				  hpd);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	ioex_set_level(IOEX_USB_A0_RETIMER_EN, 0);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 0);
		ioex_set_level(IOEX_HDMI_POWER_EN_DB, 0);
	}

	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */
static int woomax_ps8818_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_18DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_18DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX1EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_APTX2EQ_5G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_DPEQ_LEVEL,
					      PS8818_DPEQ_LEVEL_UP_MASK,
					      PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 1);
	} else {
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 0);
	}

	if (!(mux_state & USB_PD_MUX_POLARITY_INVERTED)) {
		rv = ps8818_i2c_field_update8(me, PS8818_REG_PAGE1,
					      PS8818_REG1_CRX1EQ_10G_LEVEL,
					      PS8818_EQ_LEVEL_UP_MASK,
					      PS8818_EQ_LEVEL_UP_19DB);
		rv |= ps8818_i2c_write(me, PS8818_REG_PAGE1,
				       PS8818_REG1_APRX1_DE_LEVEL, 0x02);
	}

	/* set the RX input termination */
	rv |= ps8818_i2c_field_update8(me, PS8818_REG_PAGE1, PS8818_REG1_RX_PHY,
				       PS8818_RX_INPUT_TERM_MASK,
				       PS8818_RX_INPUT_TERM_85_OHM);
	/* set register 0x40 ICP1 for 1G PD loop */
	rv |= ps8818_i2c_write(me, PS8818_REG_PAGE1, 0x40, 0x84);

	return rv;
}

static int woomax_ps8802_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* Make sure the PS8802 is awake */
	rv = ps8802_i2c_wake(me);
	if (rv)
		return rv;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8802_i2c_field_update16(me, PS8802_REG_PAGE2,
					       PS8802_REG2_USB_SSEQ_LEVEL,
					       PS8802_USBEQ_LEVEL_UP_MASK,
					       PS8802_USBEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/*Boost the DP gain */
		rv = ps8802_i2c_field_update16(me, PS8802_REG_PAGE2,
					       PS8802_REG2_DPEQ_LEVEL,
					       PS8802_DPEQ_LEVEL_UP_MASK,
					       PS8802_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 1);
	} else {
		/* Disable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 0);
	}

	/* Set extra swing level tuning at 800mV/P0 */
	rv = ps8802_i2c_field_update8(me, PS8802_REG_PAGE1,
				      PS8802_800MV_LEVEL_TUNING,
				      PS8802_EXTRA_SWING_LEVEL_P0_MASK,
				      PS8802_EXTRA_SWING_LEVEL_P0_UP_1);

	return rv;
}

const struct usb_mux usbc1_woomax_ps8818 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8818_I2C_ADDR0_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.board_set = &woomax_ps8818_mux_set,
};

/* Place holder for second mux in USBC1 chain */
struct usb_mux_chain usbc1_mux1;

static void setup_mux(void)
{
	if (ec_config_has_usbc1_retimer_ps8802()) {
		ccprints("C1 PS8802 detected");

		/*
		 * Main MUX is PS8802, secondary MUX is modified FP5
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the PS8802
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_ps8802;

		/* Set the AMD FP5 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_amd_fp5_usb_mux;
		usbc1_ps8802.board_set = &woomax_ps8802_mux_set;

		/* Don't have the AMD FP5 flip */
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	} else if (ec_config_has_usbc1_retimer_ps8818()) {
		ccprints("C1 PS8818 detected");

		/*
		 * Main MUX is FP5, secondary MUX is PS8818
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the AMD FP5
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_amd_fp5_usb_mux;

		/* Set the PS8818 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_woomax_ps8818;
	}
}

enum pi3dpx1207_usb_conf { USB_DP = 0, USB_DP_INV, USB, USB_INV, DP, DP_INV };

static uint8_t pi3dpx1207_picasso_eq[] = {
	/*usb_dp*/
	0x13,
	0x11,
	0x20,
	0x62,
	0x06,
	0x5B,
	0x5B,
	0x07,
	0x03,
	0x40,
	0xFC,
	0x42,
	0x71,
	/*usb_dp_inv */
	0x13,
	0x11,
	0x20,
	0x72,
	0x06,
	0x03,
	0x07,
	0x5B,
	0x5B,
	0x23,
	0xFC,
	0x42,
	0x71,
	/*usb*/
	0x13,
	0x11,
	0x20,
	0x42,
	0x00,
	0x03,
	0x07,
	0x07,
	0x03,
	0x00,
	0x42,
	0x42,
	0x71,
	/*usb_inv*/
	0x13,
	0x11,
	0x20,
	0x52,
	0x00,
	0x03,
	0x07,
	0x07,
	0x03,
	0x02,
	0x42,
	0x42,
	0x71,
	/*dp*/
	0x13,
	0x11,
	0x20,
	0x22,
	0x06,
	0x5B,
	0x5B,
	0x5B,
	0x5B,
	0x60,
	0xFC,
	0xFC,
	0x71,
	/*dp_inv*/
	0x13,
	0x11,
	0x20,
	0x32,
	0x06,
	0x5B,
	0x5B,
	0x5B,
	0x5B,
	0x63,
	0xFC,
	0xFC,
	0x71,
};
static uint8_t pi3dpx1207_dali_eq[] = {
	/*usb_dp*/
	0x13,
	0x11,
	0x20,
	0x62,
	0x06,
	0x5B,
	0x5B,
	0x07,
	0x07,
	0x40,
	0xFC,
	0x42,
	0x71,
	/*usb_dp_inv*/
	0x13,
	0x11,
	0x20,
	0x72,
	0x06,
	0x07,
	0x07,
	0x5B,
	0x5B,
	0x23,
	0xFC,
	0x42,
	0x71,
	/*usb*/
	0x13,
	0x11,
	0x20,
	0x42,
	0x00,
	0x07,
	0x07,
	0x07,
	0x07,
	0x00,
	0x42,
	0x42,
	0x71,
	/*usb_inv*/
	0x13,
	0x11,
	0x20,
	0x52,
	0x00,
	0x07,
	0x07,
	0x07,
	0x07,
	0x02,
	0x42,
	0x42,
	0x71,
	/*dp*/
	0x13,
	0x11,
	0x20,
	0x22,
	0x06,
	0x5B,
	0x5B,
	0x5B,
	0x5B,
	0x60,
	0xFC,
	0xFC,
	0x71,
	/*dp_inv*/
	0x13,
	0x11,
	0x20,
	0x32,
	0x06,
	0x5B,
	0x5B,
	0x5B,
	0x5B,
	0x63,
	0xFC,
	0xFC,
	0x71,
};

static int board_pi3dpx1207_mux_set(const struct usb_mux *me,
				    mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	enum pi3dpx1207_usb_conf usb_mode = 0;

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
	if (ec_config_has_usbc1_retimer_ps8802())
		rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags,
			      &pi3dpx1207_dali_eq[usb_mode * 13], 13, NULL, 0);
	else
		rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags,
			      &pi3dpx1207_picasso_eq[usb_mode * 13], 13, NULL,
			      0);

	return rv;
}

const struct pi3dpx1207_usb_control pi3dpx1207_controls[] = {
	[USBC_PORT_C0] = {
		.enable_gpio = IOEX_USB_C0_DATA_EN,
		.dp_enable_gpio = GPIO_USB_C0_IN_HPD,
	},
	[USBC_PORT_C1] = {
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3dpx1207_controls) == USBC_PORT_COUNT);

const struct usb_mux_chain usbc0_pi3dpx1207_usb_retimer = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_TCPC0,
			.i2c_addr_flags = PI3DPX1207_I2C_ADDR_FLAGS,
			.driver = &pi3dpx1207_usb_retimer,
			.board_set = &board_pi3dpx1207_mux_set,
		},
};

struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_USB_AP_MUX,
			.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
			.driver = &amd_fp5_usb_mux_driver,
		},
		.next = &usbc0_pi3dpx1207_usb_retimer,
	},
	[USBC_PORT_C1] = {
		/* Filled in dynamically at startup */
		.next = &usbc1_mux1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/*****************************************************************************
 * Use FW_CONFIG to set correct configuration.
 */

int board_usbc1_retimer_inhpd = IOEX_USB_C1_HPD_IN_DB;
static uint32_t board_ver;
static void setup_fw_config(void)
{
	cbi_get_board_version(&board_ver);

	if (board_ver >= 2)
		board_usbc1_retimer_inhpd = GPIO_USB_C1_HPD_IN_DB;

	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);

	/* Enable DP1_HPD_EC_IN interrupt */
	if (ec_config_has_hdmi_retimer_pi3hdx1204())
		gpio_enable_interrupt(GPIO_DP1_HPD_EC_IN);

	setup_base_gyro_config();
	setup_mux();
}
/* Use HOOK_PRIO_INIT_I2C + 2 to be after ioex_init(). */
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

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
	.rpm_min = 1100,
	.rpm_start = 1100,
	.rpm_max = 5120,
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
		[EC_TEMP_THRESH_HIGH] = C_TO_K(95),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
	},
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(95),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
	},
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_SOC] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

static const struct ec_response_keybd_config woomax_kb = {
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
	return &woomax_kb;
}

static void keyboard_init(void)
{
	keyscan_config.actual_key_mask[1] = 0xfe;
	keyscan_config.actual_key_mask[11] = 0xfe;
	keyscan_config.actual_key_mask[12] = 0xff;
	keyscan_config.actual_key_mask[13] = 0xff;
	keyscan_config.actual_key_mask[14] = 0xff;
}
DECLARE_HOOK(HOOK_INIT, keyboard_init, HOOK_PRIO_INIT_I2C + 1);

static void hdmi_hpd_handler(void)
{
	int hpd = gpio_get_level(GPIO_DP1_HPD_EC_IN);

	pi3hdx1204_enable(
		I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) && hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* Debounce 2 msec */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

int board_usbc_port_to_hpd_gpio_or_ioex(int port)
{
	/* USB-C0 always uses USB_C0_HPD */
	if (port == 0)
		return GPIO_USB_C0_HPD;
	/*
	 * USB-C1 OPT3 DB use IOEX_USB_C1_HPD_IN_DB for board version 1
	 * USB-C1 OPT3 DB use GPIO_USB_C1_HPD_IN_DB for board version 2
	 */
	else if (ec_config_has_mst_hub_rtd2141b())
		return (board_ver >= 2) ? GPIO_USB_C1_HPD_IN_DB :
					  IOEX_USB_C1_HPD_IN_DB;

	/* USB-C1 OPT1 DB use DP2_HPD. */
	return GPIO_DP2_HPD;
}
