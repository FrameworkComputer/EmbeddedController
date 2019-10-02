/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "board.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/als_tcs3400.h"
#include "driver/battery/max17055.h"
#include "driver/charger/rt946x.h"
#include "driver/sync.h"
#include "driver/tcpm/mt6370.h"
#include "driver/temp_sensor/tmp432.h"
#include "driver/wpc/p9221.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "power.h"
#include "power_button.h"
#include "lid_switch.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* LCM_ID is embedded in SKU_ID bit[19-16] */
#define SKU_ID_TO_LCM_ID(x)	(((x) >> PANEL_ID_BIT_POSITION) & 0xf)
#define LCM_ID_TO_SKU_ID(x)	(((x) & 0xf) << PANEL_ID_BIT_POSITION)

/* BOARD_VERSION < 5: Pull-up = 1800 mV. */
static const struct mv_to_id panels0[] = {
	{ PANEL_BOE_TV101WUM_NG0,	74 },	/* 2.2 kohm */
	{ PANEL_BOE_TV080WUM_NG0,	212 },	/* 6.8 kohm */
	{ PANEL_STA_10P,		1191 },	/* 100 kohm */
	{ PANEL_STA_08P,		1028 },	/* 68 kohm */
};
BUILD_ASSERT(ARRAY_SIZE(panels0) < PANEL_COUNT);

/* BOARD_VERSION >= 5: Pull-up = 3300 mV. */
static const struct mv_to_id panels1[] = {
	{ PANEL_BOE_TV101WUM_NG0,	136 },	/* 2.2 kohm */
	{ PANEL_BOE_TV080WUM_NG0,	387 },	/* 6.8 kohm */
	{ PANEL_STA_10P,		2184 },	/* 100 kohm */
	{ PANEL_STA_08P,		1884 },	/* 68 kohm */
};
BUILD_ASSERT(ARRAY_SIZE(panels1) < PANEL_COUNT);

BUILD_ASSERT(PANEL_COUNT <= PANEL_UNINITIALIZED);

uint8_t board_version;
uint8_t oem;
uint32_t sku = LCM_ID_TO_SKU_ID(PANEL_UNINITIALIZED);

static const struct rt946x_init_setting battery_init_setting = {
	.eoc_current = 150,
	.mivr = 4000,
	.ircmp_vclamp = 32,
	.ircmp_res = 25,
	.boost_voltage = 5050,
	.boost_current = 1500,
};

int board_read_id(enum adc_channel ch, const struct mv_to_id *table, int size)
{
	int mv = adc_read_channel(ch);
	int i;

	if (mv == ADC_READ_ERROR)
		mv = adc_read_channel(ch);

	for (i = 0; i < size; i++) {
		if (ABS(mv - table[i].median_mv) < ADC_MARGIN_MV)
			return table[i].id;
	}

	return ADC_READ_ERROR;
}

const struct rt946x_init_setting *board_rt946x_init_setting(void)
{
	return &battery_init_setting;
}

static void board_setup_panel(void)
{
	uint8_t channel;
	uint8_t dim;
	int rv = 0;

	if (board_version >= 3) {
		switch (SKU_ID_TO_LCM_ID(sku)) {
		case PANEL_BOE_TV080WUM_NG0:
		case PANEL_STA_08P:
			channel = 0xfa;
			dim = 0xc8;
			break;
		case PANEL_BOE_TV101WUM_NG0:
		case PANEL_STA_10P:
			channel = 0xfe;
			dim = 0xc4;
			break;
		default:
			return;
		}
	} else {
		/* TODO: to be removed once the boards are deprecated. */
		channel = sku & SKU_ID_PANEL_SIZE_MASK ? 0xfe : 0xfa;
		dim = sku & SKU_ID_PANEL_SIZE_MASK ? 0xc4 : 0xc8;
	}

	rv |= i2c_write8(I2C_PORT_CHARGER, RT946X_ADDR_FLAGS,
			 MT6370_BACKLIGHT_BLEN, channel);
	rv |= i2c_write8(I2C_PORT_CHARGER, RT946X_ADDR_FLAGS,
			 MT6370_BACKLIGHT_BLDIM, dim);
	rv |= i2c_write8(I2C_PORT_CHARGER, RT946X_ADDR_FLAGS,
			 MT6370_BACKLIGHT_BLPWM, 0xac);
	if (rv)
		CPRINTS("Board setup panel failed");
}

static enum panel_id board_get_panel_id(void)
{
	enum panel_id id;

	if (board_version < 3) {
		id = PANEL_DEFAULT; /* No LCM_ID. */
	} else {
		const struct mv_to_id *table = panels0;
		int size = ARRAY_SIZE(panels0);
		if (board_version >= 5) {
			table = panels1;
			size = ARRAY_SIZE(panels1);
		}
		id  = board_read_id(ADC_LCM_ID, table, size);
		if (id < PANEL_DEFAULT || PANEL_COUNT <= id)
			id = PANEL_DEFAULT;
	}
	CPRINTS("LCM ID: %d", id);
	return id;
}

#define CBI_SKU_ID_SIZE 4

int cbi_board_override(enum cbi_data_tag tag, uint8_t *buf, uint8_t *size)
{
	switch (tag) {
	case CBI_TAG_SKU_ID:
		if (*size != CBI_SKU_ID_SIZE)
			/* For old boards (board_version < 3) */
			return EC_SUCCESS;
		if (SKU_ID_TO_LCM_ID(sku) == PANEL_UNINITIALIZED)
			/* Haven't read LCM_ID */
			return EC_ERROR_BUSY;
		buf[PANEL_ID_BIT_POSITION / 8] = SKU_ID_TO_LCM_ID(sku);
		break;
	default:
		break;
	}
	return EC_SUCCESS;
}

static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_board_version(&val) == EC_SUCCESS && val <= UINT8_MAX)
		board_version = val;
	CPRINTS("Board Version: 0x%02x", board_version);

	if (cbi_get_oem_id(&val) == EC_SUCCESS && val <= PROJECT_COUNT)
		oem = val;
	CPRINTS("OEM: %d", oem);

	sku = LCM_ID_TO_SKU_ID(board_get_panel_id());

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku = val;

	CPRINTS("SKU: 0x%08x", sku);
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

static void tcpc_alert_event(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0 /* port */);
}

static void gauge_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_CHARGER);
}

#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_LCM_ID] = {"LCM_ID", 3300, 4096, 0, STM32_AIN(10)},
	[ADC_EC_SKU_ID] = {"EC_SKU_ID", 3300, 4096, 0, STM32_AIN(8)},
	[ADC_BATT_ID] = {"BATT_ID", 3300, 4096, 0, STM32_AIN(7)},
	[ADC_USBC_THERM] = {"USBC_THERM", 3300, 4096, 0, STM32_AIN(14),
		STM32_ADC_SMPR_239_5_CY},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"charger",   I2C_PORT_CHARGER,   400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc0",     I2C_PORT_TCPC0,     400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"als",       I2C_PORT_ALS,       400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"battery",   I2C_PORT_BATTERY,   400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"accelgyro", I2C_PORT_ACCEL,     400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"eeprom",    I2C_PORT_EEPROM,    400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_IN_SLEEP_L,   POWER_SIGNAL_ACTIVE_LOW,  "AP_IN_S3_L"},
	{GPIO_PMIC_EC_RESETB,  POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

#ifdef CONFIG_TEMP_SENSOR_TMP432
/* Temperature sensors data; must be in same order as enum temp_sensor_id. */
const struct temp_sensor_t temp_sensors[] = {
	{"TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_LOCAL, 4},
	{"TMP432_Sensor_1", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE1, 4},
	{"TMP432_Sensor_2", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE2, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	{{0, 0, 0}, 0, 0}, /* TMP432_Internal */
	{{0, 0, 0}, 0, 0}, /* TMP432_Sensor_1 */
	{{0, 0, 0}, 0, 0}, /* TMP432_Sensor_2 */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
#endif

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = MT6370_TCPC_I2C_ADDR_FLAGS,
		},
		.drv = &mt6370_tcpm_drv},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &virtual_usb_mux_driver,
		.hpd_update = &virtual_hpd_update,
	},
};

void board_reset_pd_mcu(void)
{
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	return status;
}

int board_set_active_charge_port(int charge_port)
{
	CPRINTS("New chg p%d", charge_port);

	switch (charge_port) {
	case 0:
		/* Don't charge from a source port except wireless charging*/
#ifdef CONFIG_WIRELESS_CHARGER_P9221_R7
		if (board_vbus_source_enabled(charge_port)
			&& !wpc_chip_is_online())
#else
		if (board_vbus_source_enabled(charge_port))
#endif
			return -1;
		break;
	case CHARGE_PORT_NONE:
		/*
		 * To ensure the fuel gauge (max17055) is always powered
		 * even when battery is disconnected, keep VBAT rail on but
		 * set the charging current to minimum.
		 */
		charger_set_current(0);
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
			       CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

int extpower_is_present(void)
{
	return tcpm_get_vbus_level(0);
}

int pd_snk_is_vbus_provided(int port)
{
	if (port)
		panic("Invalid charge port\n");

	return rt946x_is_vbus_ready();
}

/*
 * Threshold to detect USB-C board. If the USB-C board isn't connected,
 * USBC_THERM is floating thus the ADC pin should read about the pull-up
 * voltage. If it's connected, the voltage is capped by the resistor (429k)
 * place in parallel to the thermistor. 3.3V x 429k/(39k + 429k) = 3.025V
 */
#define USBC_THERM_THRESHOLD 3025

static void board_init(void)
{
#ifdef SECTION_IS_RO
	/* If USB-C board isn't connected, the device is being assembled.
	 * We cut off the battery until the assembly is done for better yield.
	 * Timing is ok because STM32F0 initializes ADC on demand. */
	if (board_version > 0x02) {
		int mv = adc_read_channel(ADC_USBC_THERM);
		if (mv == ADC_READ_ERROR)
			mv = adc_read_channel(ADC_USBC_THERM);
		CPRINTS("USBC_THERM=%d", mv);
		if (mv > USBC_THERM_THRESHOLD) {
			cflush();
			board_cut_off_battery();
		}
	}
#endif
	/* Set SPI1 PB13/14/15 pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xfc000000;

	/* Enable TCPC alert interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_CHARGER_INT_ODL);

#ifdef SECTION_IS_RW
#ifdef CONFIG_WIRELESS_CHARGER_P9221_R7
	/* Enable Wireless charger interrupts */
	gpio_enable_interrupt(GPIO_P9221_INT_ODL);
#endif
	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACCEL_INT_ODL);

	/* Enable interrupt for the TCS3400 color light sensor */
	if (board_version >= 4)
		gpio_enable_interrupt(GPIO_TCS3400_INT_ODL);

	/* Enable interrupt for the camera vsync. */
	gpio_enable_interrupt(GPIO_SYNC_INT);
#endif /* SECTION_IS_RW */

	/* Enable interrupt from PMIC. */
	gpio_enable_interrupt(GPIO_PMIC_EC_RESETB);

	/* Enable gauge interrupt from max17055 */
	gpio_enable_interrupt(GPIO_GAUGE_INT_ODL);
	board_setup_panel();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#ifdef SECTION_IS_RW
static void usb_pd_connect(void)
{
	/* VBUS from p9221 is already zero as it's disabled by NCP3902 */
	p9221_notify_vbus_change(0);
	rt946x_toggle_bc12_detection();
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, usb_pd_connect, HOOK_PRIO_DEFAULT);
#endif

void board_config_pre_init(void)
{
	STM32_RCC_AHBENR |= STM32_RCC_HB_DMA1;
	/*
	 * Remap USART1 and SPI2 DMA:
	 *
	 * Ch4: USART1_TX / Ch5: USART1_RX (1000)
	 * Ch6: SPI2_RX / Ch7: SPI2_TX (0011)
	 */
	STM32_DMA_CSELR(STM32_DMAC_CH4) = (8 << 12) | (8 << 16) |
					  (3 << 20) | (3 << 24);
}

/* Motion sensors */
/* Mutexes */
#ifdef SECTION_IS_RW
static struct mutex g_lid_mutex;

static struct bmi160_drv_data_t g_bmi160_data;

static struct als_drv_data_t g_tcs3400_data = {
	.als_cal.scale = 1,
	.als_cal.uscale = 0,
	.als_cal.offset = 0,
	.als_cal.channel_scale = {
		.k_channel_scale = ALS_CHANNEL_SCALE(1.0),   /* kc from VPD */
		.cover_scale = ALS_CHANNEL_SCALE(0.9),       /* CT */
	},
};

static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	.rgb_cal[X] = {
		.offset = 15, /* 15.65956688 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.04592318),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0.06756278),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(-0.05885579),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.12021096),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kr */
			.cover_scale = ALS_CHANNEL_SCALE(0.6)
		}
	},
	.rgb_cal[Y] = {
		.offset = 8, /* 8.75943638 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.07786953),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0.18940035),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(-0.0524428),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.09092403),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kg */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		}
	},
	.rgb_cal[Z] = {
		.offset = -21, /* -21.92665481 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.18981975),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0.5351057),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(-0.01858507),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(-0.01793189),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0),   /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(1.5)
		}
	},
	.saturation.again = TCS_DEFAULT_AGAIN,
	.saturation.atime = TCS_DEFAULT_ATIME,
};

/* Matrix to rotate accelerometer into standard reference frame */
const mat33_fp_t lid_standard_ref = {
	{ 0,  FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[LID_ACCEL] = {
	 .name = "Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 4,  /* g */
	 .min_frequency = BMI160_ACCEL_MIN_FREQ,
	 .max_frequency = BMI160_ACCEL_MAX_FREQ,
	 .config = {
		 /* Enable accel in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 10000 | ROUND_UP_FLAG,
			 .ec_rate = 100 * MSEC,
		 },
	 },
	},
	[LID_GYRO] = {
	 .name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &lid_standard_ref,
	 .min_frequency = BMI160_GYRO_MIN_FREQ,
	 .max_frequency = BMI160_GYRO_MAX_FREQ,
	},
	[CLEAR_ALS] = {
	 .name = "Clear Light",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_TCS3400,
	 .type = MOTIONSENSE_TYPE_LIGHT,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &tcs3400_drv,
	 .drv_data = &g_tcs3400_data,
	 .port = I2C_PORT_ALS,
	 .i2c_spi_addr_flags = TCS3400_I2C_ADDR_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 0x10000, /* scale = 1x, uscale = 0 */
	 .min_frequency = TCS3400_LIGHT_MIN_FREQ,
	 .max_frequency = TCS3400_LIGHT_MAX_FREQ,
	 .config = {
		 /* Run ALS sensor in S0 */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 1000,
		},
	 },
	},
	[RGB_ALS] = {
	.name = "RGB Light",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_TCS3400,
	 .type = MOTIONSENSE_TYPE_LIGHT_RGB,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &tcs3400_rgb_drv,
	 .drv_data = &g_tcs3400_rgb_data,
	 /*.port=I2C_PORT_ALS,*/ /* Unused. RGB channels read by CLEAR_ALS. */
	 .rot_standard_ref = NULL,
	 .default_range = 0x10000, /* scale = 1x, uscale = 0 */
	 .min_frequency = 0, /* 0 indicates we should not use sensor directly */
	 .max_frequency = 0, /* 0 indicates we should not use sensor directly */
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
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);
#endif /* SECTION_IS_RW */

int board_allow_i2c_passthru(int port)
{
	return (port == I2C_PORT_VIRTUAL_BATTERY);
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
}

int board_get_fod(uint8_t **fod)
{
	*fod = NULL;
	return 0;
}

int board_get_epp_fod(uint8_t **fod)
{
	*fod = NULL;
	return 0;
}

