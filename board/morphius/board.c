/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Morphius board configuration */

#include "adc.h"
#include "battery_smart.h"
#include "button.h"
#include "cbi_ssfc.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/ppc/aoz1380_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/pi3dpx1207.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/temp_sensor/tmp432.h"
#include "driver/usb_mux/amd_fp5.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "lid_switch.h"
#include "mkbp_event.h"
#include "power.h"
#include "power_button.h"
#include "ps2_chip.h"
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
#include "usbc_ppc.h"

static void hdmi_hpd_interrupt_v2(enum ioex_signal signal);
static void hdmi_hpd_interrupt_v3(enum gpio_signal signal);
static void board_gmr_tablet_switch_isr(enum gpio_signal signal);

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

static bool support_aoz_ppc;
static bool ignore_c1_dp;

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
				 { FLOAT_TO_FP(1), 0, 0 },
				 { 0, 0, FLOAT_TO_FP(-1) } };
const mat33_fp_t base_standard_ref_1 = { { FLOAT_TO_FP(-1), 0, 0 },
					 { 0, FLOAT_TO_FP(1), 0 },
					 { 0, 0, FLOAT_TO_FP(-1) } };
mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
				{ FLOAT_TO_FP(-1), 0, 0 },
				{ 0, 0, FLOAT_TO_FP(1) } };

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;
static struct icm_drv_data_t g_icm426xx_data;

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
	 .rot_standard_ref = (const mat33_fp_t *)&lid_standard_ref,
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
	 .rot_standard_ref = (const mat33_fp_t *)&base_standard_ref,
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
	 .rot_standard_ref = (const mat33_fp_t *)&base_standard_ref,
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
	[PWM_CH_POWER_LED] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
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
	.de_offset = PI3HDX1204_DE_DB_MINUS7,
};

/*****************************************************************************
 * Base Gyro Sensor dynamic configuration
 */
static enum ec_ssfc_base_gyro_sensor base_gyro_config = SSFC_BASE_GYRO_NONE;

enum ec_ssfc_base_gyro_sensor get_base_gyro_sensor(void)
{
	switch (get_cbi_ssfc_base_sensor()) {
	case SSFC_BASE_GYRO_NONE:
		return ec_config_has_base_gyro_sensor();
	default:
		return get_cbi_ssfc_base_sensor();
	}
}

static void setup_base_gyro_config(void)
{
	base_gyro_config = get_base_gyro_sensor();

	switch (base_gyro_config) {
	case SSFC_BASE_GYRO_BMI160:
		ccprints("BASE GYRO is BMI160");
		break;
	case SSFC_BASE_GYRO_ICM426XX:
		motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
		motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
		ccprints("BASE GYRO is ICM426XX");
		break;
	default:
		break;
	}
}

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_gyro_config) {
	case SSFC_BASE_GYRO_BMI160:
		bmi160_interrupt(signal);
		break;
	case SSFC_BASE_GYRO_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	default:
		break;
	}
}

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */

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
		usbc1_mux1.mux = &usbc1_ps8818;
	}
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
static uint32_t board_ver;
enum gpio_signal gpio_ec_ps2_reset = GPIO_EC_PS2_RESET_V1;
int board_usbc1_retimer_inhpd = GPIO_USB_C1_HPD_IN_DB_V1;

static void setup_v0_charger(void)
{
	cbi_get_board_version(&board_ver);

	if (board_ver <= 2)
		chg_chips[0].i2c_port = I2C_PORT_CHARGER_V0;
}
/*
 * Use HOOK_PRIO_INIT_I2C so we re-map before charger_chips_init()
 * talks to the charger.
 */
DECLARE_HOOK(HOOK_INIT, setup_v0_charger, HOOK_PRIO_INIT_I2C);

int board_usbc_port_to_hpd_gpio_or_ioex(int port)
{
	/* USB-C0 always uses USB_C0_HPD (= DP3_HPD). */
	if (port == 0)
		return GPIO_USB_C0_HPD;

	/*
	 * USB-C1 OPT3 DB
	 *    version_2 uses EC_DP1_HPD
	 *    version_3 uses DP1_HPD via RTD2141B MST hub to drive AP
	 *    HPD, EC drives MST hub HPD input from USB-PD messages.
	 *
	 * This would have been ec_config_has_usbc1_retimer_ps8802
	 * on version_2 hardware but the result is the same and
	 * this will be removed when version_2 hardware is retired.
	 */
	else if (ec_config_has_mst_hub_rtd2141b())
		return (board_ver >= 4) ? GPIO_USB_C1_HPD_IN_DB_V1 :
		       (board_ver == 3) ? IOEX_USB_C1_HPD_IN_DB :
					  GPIO_EC_DP1_HPD;

	/* USB-C1 OPT1 DB uses DP2_HPD. */
	return GPIO_DP2_HPD;
}

static void board_remap_gpio(void)
{
	int ppc_id = 0;

	if (board_ver >= 3) {
		int rv;

		gpio_ec_ps2_reset = GPIO_EC_PS2_RESET_V1;
		ccprintf("GPIO_EC_PS2_RESET_V1\n");

		/*
		 * TODO(dbrockus@): remove code when older version_2
		 * hardware is retired and no longer needed
		 */
		rv = ioex_set_flags(IOEX_HDMI_POWER_EN_DB, GPIO_OUT_LOW);
		rv |= ioex_set_flags(IOEX_USB_C1_PPC_ILIM_3A_EN, GPIO_OUT_LOW);
		if (rv)
			ccprintf("IOEX Board>=3 Remap FAILED\n");

		if (ec_config_has_hdmi_retimer_pi3hdx1204())
			gpio_enable_interrupt(GPIO_DP1_HPD_EC_IN);
	} else {
		gpio_ec_ps2_reset = GPIO_EC_PS2_RESET_V0;
		ccprintf("GPIO_EC_PS2_RESET_V0\n");

		/*
		 * TODO(dbrockus@): remove code when older version_2
		 * hardware is retired and no longer needed
		 */
		if (ec_config_has_mst_hub_rtd2141b())
			ioex_enable_interrupt(IOEX_MST_HPD_OUT);

		if (ec_config_has_hdmi_retimer_pi3hdx1204())
			ioex_enable_interrupt(IOEX_HDMI_CONN_HPD_3V3_DB);
	}

	if (board_ver >= 4)
		board_usbc1_retimer_inhpd = GPIO_USB_C1_HPD_IN_DB_V1;
	else
		board_usbc1_retimer_inhpd = IOEX_USB_C1_HPD_IN_DB;

	ioex_get_level(IOEX_PPC_ID, &ppc_id);

	support_aoz_ppc = (board_ver == 3) || ((board_ver >= 4) && !ppc_id);
	if (support_aoz_ppc) {
		ccprintf("DB USBC PPC aoz1380\n");
		ppc_chips[USBC_PORT_C1].drv = &aoz1380_drv;
	}
}

static void setup_fw_config(void)
{
	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);

	/* Enable PS2 power interrupts */
	gpio_enable_interrupt(GPIO_EN_PWR_TOUCHPAD_PS2);

	ps2_enable_channel(NPCX_PS2_CH0, 1, send_aux_data_to_host_interrupt);

	setup_mux();

	board_remap_gpio();

	setup_base_gyro_config();
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
	.rpm_min = 1800,
	.rpm_start = 3000,
	.rpm_max = 5200,
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

	case TEMP_SENSOR_5V_REGULATOR:
		/* thermistor is not powered in G3 */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			return EC_ERROR_NOT_POWERED;

		channel = ADC_TEMP_SENSOR_5V_REGULATOR;
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
	[ADC_TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
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
	[TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_5V_REGULATOR,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
	[TEMP_SENSOR_SSD] = {
		.name = "SSD",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = tmp432_get_val,
		.idx = TMP432_IDX_LOCAL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(105),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
	},
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

/* Battery functions */
#define SB_OPTIONALMFG_FUNCTION2 0x26
#define SMART_CHARGE_SUPPORT 0x01
#define SMART_CHARGE_ENABLE 0x02
#define SB_SMART_CHARGE_ENABLE 1
#define SB_SMART_CHARGE_DISABLE 0

static void sb_smart_charge_mode(int enable)
{
	int val, rv;

	rv = sb_read(SB_OPTIONALMFG_FUNCTION2, &val);
	if (rv)
		return;
	if (val & SMART_CHARGE_SUPPORT) {
		if (enable)
			val |= SMART_CHARGE_ENABLE;
		else
			val &= ~SMART_CHARGE_ENABLE;
		sb_write(SB_OPTIONALMFG_FUNCTION2, val);
	}
}

__override void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_FAULT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		if (support_aoz_ppc)
			aoz1380_interrupt(USBC_PORT_C1);
		else
			nx20p348x_interrupt(USBC_PORT_C1);
		break;

	default:
		break;
	}
}

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
__override int
board_aoz1380_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv;

	/* Use the TCPC to set the current limit */
	if (port == 0) {
		rv = ioex_set_level(IOEX_USB_C0_PPC_ILIM_3A_EN,
				    (rp == TYPEC_RP_3A0) ? 1 : 0);
	} else if (board_ver >= 3) {
		rv = ioex_set_level(IOEX_USB_C1_PPC_ILIM_3A_EN,
				    (rp == TYPEC_RP_3A0) ? 1 : 0);
	} else {
		rv = 1;
	}

	return rv;
}

static void trackpoint_reset_deferred(void)
{
	gpio_set_level(gpio_ec_ps2_reset, 1);
	crec_msleep(2);
	gpio_set_level(gpio_ec_ps2_reset, 0);
	crec_msleep(10);
}
DECLARE_DEFERRED(trackpoint_reset_deferred);

void send_aux_data_to_device(uint8_t data)
{
	ps2_transmit_byte(NPCX_PS2_CH0, data);
}

void ps2_pwr_en_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&trackpoint_reset_deferred_data, MSEC);
}

static int check_hdmi_hpd_status(void)
{
	int hpd = 0;

	if (board_ver < 3)
		ioex_get_level(IOEX_HDMI_CONN_HPD_3V3_DB, &hpd);
	else
		hpd = gpio_get_level(GPIO_DP1_HPD_EC_IN);

	return hpd;
}

/*****************************************************************************
 * Board suspend / resume
 */

static void board_chipset_resume(void)
{
	/* Normal charge current */
	sb_smart_charge_mode(SB_SMART_CHARGE_DISABLE);
	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 1);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		if (board_ver >= 3) {
			ioex_set_level(IOEX_HDMI_POWER_EN_DB, 1);
			crec_msleep(PI3HDX1204_POWER_ON_DELAY_MS);
		}
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
				  check_hdmi_hpd_status());
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend_delay(void)
{
	ignore_c1_dp = false;
}
DECLARE_DEFERRED(board_chipset_suspend_delay);

static void board_chipset_suspend(void)
{
	/* SMART charge current */
	sb_smart_charge_mode(SB_SMART_CHARGE_ENABLE);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 0);
		if (board_ver >= 3)
			ioex_set_level(IOEX_HDMI_POWER_EN_DB, 0);
	}

	/* Wait 500ms before allowing DP event to cause resume. */
	if (ec_config_has_mst_hub_rtd2141b() &&
	    (dp_flags[USBC_PORT_C1] & DP_FLAGS_DP_ON)) {
		ignore_c1_dp = true;
		hook_call_deferred(&board_chipset_suspend_delay_data,
				   500 * MSEC);
	}

	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * Power signals
 */

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

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ 3, 0 },   { 2, 2 }, { 2, 3 },	  { 1, 2 }, { 2, 5 }, { 2, 4 },
	{ 2, 1 },   { 2, 7 }, { 2, 6 },	  { 1, 5 }, { 2, 0 }, { 3, 1 },
	{ 1, 7 },   { 1, 6 }, { -1, -1 }, { 1, 3 }, { 1, 4 }, { -1, -1 },
	{ -1, -1 }, { 0, 7 }, { 0, 6 },	  { 1, 0 }, { 1, 1 }, { 0, 5 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

/*****************************************************************************
 * MST hub
 *
 * TODO(dbrockus@): remove VERSION_2 code when older version of hardware is
 * retired and no longer needed
 */
static void mst_hpd_handler(void)
{
	int hpd = 0;

	/*
	 * Ensure level on GPIO_EC_DP1_HPD matches IOEX_MST_HPD_OUT, in case
	 * we got out of sync.
	 */
	ioex_get_level(IOEX_MST_HPD_OUT, &hpd);
	gpio_set_level(GPIO_EC_DP1_HPD, hpd);
	ccprints("MST HPD %d", hpd);
}
DECLARE_DEFERRED(mst_hpd_handler);

void mst_hpd_interrupt(enum ioex_signal signal)
{
	/*
	 * Goal is to pass HPD through from DB OPT3 MST hub to AP's DP1.
	 * Immediately invert GPIO_EC_DP1_HPD, to pass through the edge on
	 * IOEX_MST_HPD_OUT. Then check level after 2 msec debounce.
	 */
	int hpd = !gpio_get_level(GPIO_EC_DP1_HPD);

	gpio_set_level(GPIO_EC_DP1_HPD, hpd);
	hook_call_deferred(&mst_hpd_handler_data, (2 * MSEC));
}

static void hdmi_hpd_handler(void)
{
	/* Pass HPD through from DB OPT1 HDMI connector to AP's DP1. */
	int hpd = check_hdmi_hpd_status();

	gpio_set_level(GPIO_EC_DP1_HPD, hpd);
	ccprints("HDMI HPD %d", hpd);
	pi3hdx1204_enable(
		I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) && hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

static void hdmi_hpd_interrupt_v2(enum ioex_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

static void hdmi_hpd_interrupt_v3(enum gpio_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

static void board_gmr_tablet_switch_isr(enum gpio_signal signal)
{
	/* Board version more than 3, DUT support GMR sensor */
	if (board_ver >= 3)
		gmr_tablet_switch_isr(signal);
}

int board_sensor_at_360(void)
{
	/*
	 * Board version >= 3 supports GMR sensor. For older boards return 0
	 * indicating not in 360-degree mode and rely on lid angle for tablet
	 * mode.
	 */
	if (board_ver >= 3)
		return !gpio_get_level(GPIO_TABLET_MODE_L);

	return 0;
}

/*
 * b/167949458: Suppress setting the host event for 500ms after entering S3.
 * Otherwise turning off the MST hub in S3 (via IOEX_HDMI_DATA_EN_DB) causes
 * a VDM:Attention that immediately wakes us back up from S3.
 */
__override void pd_notify_dp_alt_mode_entry(int port)
{
	if (port == USBC_PORT_C1 && ignore_c1_dp)
		return;
	cprints(CC_USBPD, "Notifying AP of DP Alt Mode Entry...");
	mkbp_send_event(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED);
}
