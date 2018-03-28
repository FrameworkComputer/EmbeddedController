/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "common.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/bc12/bq24392.h"
#include "driver/charger/bd9995x.h"
#include "driver/ppc/nx20p3483.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "tcpci.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define USB_PD_PORT_ANX74XX	0
#define USB_PD_PORT_PS8751	1

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
	    !gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_L:
		nx20p3483_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_L:
		nx20p3483_interrupt(1);
		break;

	default:
		break;
	}
}

/* Must come after other header files and GPIO interrupts*/
#include "gpio_list.h"

/* Wake pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus C0 sensing (10x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = {
		"VBUS_C0", NPCX_ADC_CH4, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	/* Vbus C1 sensing (10x voltage divider). PPVAR_USB_C1_VBUS */
	[ADC_VBUS_C1] = {
		"VBUS_C1", NPCX_ADC_CH9, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	/* Board ID 1. Least Significant nibble */
	[ADC_BRD_ID1] = {
		"BRD_ID1", NPCX_ADC_CH6, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
	/* Board ID 2. Most Significant nibble */
	[ADC_BRD_ID2] = {
		"BRD_ID2", NPCX_ADC_CH7, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);


/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_POWER_S0IX
	{GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED"},
#endif
	{GPIO_PCH_SLP_S3_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,   POWER_SIGNAL_ACTIVE_HIGH, "SLP_S4_DEASSERTED"},
	{GPIO_SUSPWRDNACK,    POWER_SIGNAL_ACTIVE_HIGH,
	 "SUSPWRDNACK_DEASSERTED"},

	{GPIO_ALL_SYS_PGOOD,  POWER_SIGNAL_ACTIVE_HIGH, "ALL_SYS_PGOOD"},
	{GPIO_RSMRST_L_PGOOD, POWER_SIGNAL_ACTIVE_HIGH, "RSMRST_L"},
	{GPIO_PP3300_PG,      POWER_SIGNAL_ACTIVE_HIGH, "PP3300_PG"},
	{GPIO_PP5000_PG,      POWER_SIGNAL_ACTIVE_HIGH, "PP5000_PG"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
/* TODO(b/74387239): increase I2C bus speeds after bringup. */
	{"battery", I2C_PORT_BATTERY, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"charger", I2C_PORT_CHARGER, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
	{"sensor",  I2C_PORT_SENSOR,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* USB-A port configuration */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
	/* TODO(b/74388692): Add second port control after hardware fix. */
};

/* Called by APL power state machine when transitioning from G3 to S5 */
static void chipset_pre_init(void)
{
	/* Enable 5.0V and 3.3V rails, and wait for Power Good */
	gpio_set_level(GPIO_EN_PP5000, 1);
	gpio_set_level(GPIO_EN_PP3300, 1);
	while (!gpio_get_level(GPIO_PP5000_PG) ||
	       !gpio_get_level(GPIO_PP3300_PG))
		;

	/* Enable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
	/* Enable Trackpad Power when chipset is in S0 */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* Disable Trackpad Power when chipset transitions to sleep state */
	gpio_set_level(GPIO_EN_P3300_TRACKPAD_ODL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/* Called by APL power state machine when transitioning to G3. */
void chipset_do_shutdown(void)
{
	/* Disable PMIC */
	gpio_set_level(GPIO_PMIC_EN, 0);

	/* Disable 5.0V and 3.3V rails, and wait until they power down. */
	gpio_set_level(GPIO_EN_PP5000, 0);
	gpio_set_level(GPIO_EN_PP3300, 0);
	while (gpio_get_level(GPIO_PP5000_PG) ||
	       gpio_get_level(GPIO_PP3300_PG))
		;
}

static void board_init(void)
{

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

enum adc_channel board_get_vbus_adc(int port)
{
	return port ? ADC_VBUS_C1 : ADC_VBUS_C0;
}

/**
 * Reset all system PD/TCPC MCUs -- currently only called from
 * handle_pending_reboot() in common/power.c just before hard
 * resetting the system. This logic is likely not needed as the
 * PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/* TODO(b/74127309): Flesh out USB code */
}

int board_set_active_charge_port(int port)
{
	/* TODO(b/74127309): Flesh out USB code */
	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* TODO(b/74127309): Flesh out USB code */
}

void board_tcpc_init(void)
{
	int count = 0;
	int port;

	/* Wait for disconnected battery to wake up */
	while (battery_hw_present() == BP_YES &&
	       battery_is_present() == BP_NO) {
		usleep(100 * MSEC);
		/* Give up waiting after 1 second */
		if (++count > 10) {
			ccprintf("TCPC_init: 1 second w/no battery\n");
			break;
		}
	}

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_PD_C0_INT_L);
	gpio_enable_interrupt(GPIO_USB_PD_C1_INT_L);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

static const int UNKNOWN_BRD_ID = -1;
static const int board_id_thresh_mv[] = {
	/* Vin = 3.3V, Ideal voltage, R2 values listed below */
	/* R1 = 51.1 kOhm */
	200,  /* 124 mV, 2.0 Kohm */
	366,  /* 278 mV, 4.7 Kohm */
	550,  /* 456 mV, 8.2  Kohm */
	752,  /* 644 mV, 12.4 Kohm */
	927,  /* 860 mV, 18.0 Kohm */
	1073, /* 993 mV, 22.0 Kohm */
	1235, /* 1152 mV, 27.4 Kohm */
	1386, /* 1318 mV, 34.0 Kohm */
	1552, /* 1453 mV, 40.2 Kohm */
	/* R1 = 10.0 kOhm */
	1739, /* 1650 mV, 10.0 Kohm */
	1976, /* 1827 mV, 12.4 Kohm */
	2197, /* 2121 mV, 18.0 Kohm */
	2344, /* 2269 mV, 22.0 Kohm */
	2484, /* 2418 mV, 27.4 Kohm */
	2636, /* 2550 mV, 34.0 Kohm */
	2823, /* 2721 mV, 47.0 Kohm */
};

static int read_board_id_adc(enum adc_channel chan)
{
	int mv;
	int i;

	mv = adc_read_channel(chan);
	cprints(CC_SYSTEM, "BOARD ID ADC %d = %d mV",
		chan == ADC_BRD_ID1 ? 1 : 2, mv);

	if (mv == ADC_READ_ERROR)
		return UNKNOWN_BRD_ID;

	for (i = 0; i < ARRAY_SIZE(board_id_thresh_mv); i++)
		if (mv < board_id_thresh_mv[i])
			return i;

	return UNKNOWN_BRD_ID;
}


int board_get_version(void)
{
	static int version = UNKNOWN_BRD_ID;
	int level;

	if (version != UNKNOWN_BRD_ID)
		return version;

	/* Enabled Board ID circuit and wait for it to stabilize. */
	gpio_set_level(GPIO_EC_BRD_ID_EN, 1);
	msleep(1);

	level = read_board_id_adc(ADC_BRD_ID1);
	if (level == UNKNOWN_BRD_ID)
		goto error;
	version = level & 0x0F;

	level = read_board_id_adc(ADC_BRD_ID2);
	if (level == UNKNOWN_BRD_ID)
		goto error;
	version = version | ((level & 0x0F) << 4);

	gpio_set_level(GPIO_EC_BRD_ID_EN, 0);
	cprints(CC_SYSTEM, "Board version: %d", version);
	return version;

error:
	gpio_set_level(GPIO_EC_BRD_ID_EN, 0);
	cprints(CC_SYSTEM, "Board version unknown!");
	version = UNKNOWN_BRD_ID;
	return version;
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
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

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0, 0,  FLOAT_TO_FP(1)}
};

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct stprivate_data g_lsm6dsm_data;

/* Drivers */
/* TODO(b/74602071): Tune sensor cfg after the board is received */
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
	 .addr = KX022_ADDR1,
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 4, /* g */
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
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_lsm6dsm_data,
	 .port = I2C_PORT_SENSOR,
	 .addr = LSM6DSM_ADDR0,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 4,  /* g */
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
		 /* Sensor on for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		 },
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_lsm6dsm_data,
	 .port = I2C_PORT_SENSOR,
	 .addr = LSM6DSM_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = ANX74XX_I2C_ADDR1,
		.drv = &anx74xx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.port_addr = USB_PD_PORT_ANX74XX,
		.driver = &anx74xx_tcpm_usb_mux_driver,
		.hpd_update = &anx74xx_tcpc_update_hpd_status,
	},
	[USB_PD_PORT_PS8751] = {
		.port_addr = USB_PD_PORT_PS8751,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

const struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = NX20P3483_ADDR2,
		.drv = &nx20p3483_drv,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr = NX20P3483_ADDR2,
		.drv = &nx20p3483_drv,
		.flags = PPC_CFG_FLAGS_GPIO_CONTROL,
		.snk_gpio = GPIO_USB_C1_CHARGE_ON,
		.src_gpio = GPIO_EN_USB_C1_5V_OUT,
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* BC 1.2 chip Configuration */
const struct bq24392_config_t bq24392_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
	[USB_PD_PORT_PS8751] = {
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = BQ24392_FLAGS_CHG_DET_ACTIVE_LOW,
	},
};
