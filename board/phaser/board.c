/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Phaser board-specific configuration */

#include "adc.h"
#include "battery_smart.h"
#include "button.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_lis2dh.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/anx7447.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system_chip.h"
#include "tablet_mode.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define USB_PD_PORT_ANX7447 0
#define USB_PD_PORT_PS8751 1

static uint8_t sku_id;
static bool support_syv_ppc;
static uint8_t is_psl_hibernate;

/* Check PPC ID and board version to decide which one ppc is used. */
static bool board_is_support_syv_ppc(void)
{
	uint32_t board_version = 0;

	if (cbi_get_board_version(&board_version) != EC_SUCCESS)
		CPRINTSUSB("Get board version failed.");

	if ((board_version >= 5) && (gpio_get_level(GPIO_PPC_ID)))
		return true;

	return false;
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_PD_C0_INT_ODL:
		if (support_syv_ppc)
			syv682x_interrupt(0);
		else
			nx20p348x_interrupt(0);
		break;

	case GPIO_USB_PD_C1_INT_ODL:
		if (support_syv_ppc)
			syv682x_interrupt(1);
		else
			nx20p348x_interrupt(1);
		break;

	default:
		break;
	}
}

/* Must come after other header files and GPIO interrupts*/
#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_AMB] = { "TEMP_AMB", NPCX_ADC_CH0, ADC_MAX_VOLT,
				  ADC_READ_MAX + 1, 0 },
	[ADC_TEMP_SENSOR_CHARGER] = { "TEMP_CHARGER", NPCX_ADC_CH1,
				      ADC_MAX_VOLT, ADC_READ_MAX + 1, 0 },
	/* Vbus sensing (1/10 voltage divider). */
	[ADC_VBUS_C0] = { "VBUS_C0", NPCX_ADC_CH9, ADC_MAX_VOLT * 10,
			  ADC_READ_MAX + 1, 0 },
	[ADC_VBUS_C1] = { "VBUS_C1", NPCX_ADC_CH4, ADC_MAX_VOLT * 10,
			  ADC_READ_MAX + 1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

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

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate lid and base sensor into standard reference frame */
const mat33_fp_t standard_rot_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				      { 0, FLOAT_TO_FP(-1), 0 },
				      { 0, 0, FLOAT_TO_FP(1) } };

/* sensor private data */
static struct stprivate_data g_lis2dh_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* Drivers */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DE,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dh_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_lis2dh_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2DH_ADDR1_FLAGS,
		.rot_standard_ref = &standard_rot_ref,
		/* We only use 2g because its resolution is only 8-bits */
		.default_range = 2, /* g */
		.min_frequency = LIS2DH_ODR_MIN_VAL,
		.max_frequency = LIS2DH_ODR_MAX_VAL,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.rot_standard_ref = &standard_rot_ref,
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
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
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_GYRO),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &standard_rot_ref,
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int board_is_convertible(void)
{
	return sku_id == 2 || sku_id == 3 || sku_id == 4 || sku_id == 5 ||
	       sku_id == 255;
}

static void board_update_sensor_config_from_sku(void)
{
	if (board_is_convertible()) {
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable Base Accel interrupt */
		gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_BASE_SIXAXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku_id = val;
	ccprints("SKU: 0x%04x", sku_id);

	board_update_sensor_config_from_sku();

	support_syv_ppc = board_is_support_syv_ppc();

	/* Please correct the SKU ID checking if it is not right */
	if (sku_id == 1 || sku_id == 2 || sku_id == 3 || sku_id == 4)
		is_psl_hibernate = 0;
	else
		is_psl_hibernate = 1;
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

static void system_psl_type_sel(int psl_no, uint32_t flags)
{
	/* Set PSL input events' type as level or edge trigger */
	if ((flags & GPIO_INT_F_HIGH) || (flags & GPIO_INT_F_LOW))
		CLEAR_BIT(NPCX_GLUE_PSL_CTS, psl_no + 4);
	else if ((flags & GPIO_INT_F_RISING) || (flags & GPIO_INT_F_FALLING))
		SET_BIT(NPCX_GLUE_PSL_CTS, psl_no + 4);

	/*
	 * Set PSL input events' polarity is low (high-to-low) active or
	 * high (low-to-high) active
	 */
	if (flags & GPIO_HIB_WAKE_HIGH)
		SET_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_no);
	else
		CLEAR_BIT(NPCX_DEVALT(ALT_GROUP_D), 2 * psl_no);
}

int system_config_psl_mode(enum gpio_signal signal)
{
	int psl_no;
	const struct gpio_info *g = gpio_list + signal;

	if (g->port == GPIO_PORT_D && g->mask == MASK_PIN2) /* GPIOD2 */
		psl_no = 0;
	else if (g->port == GPIO_PORT_0 && (g->mask & 0x07)) /* GPIO00/01/02 */
		psl_no = GPIO_MASK_TO_NUM(g->mask) + 1;
	else
		return 0;

	system_psl_type_sel(psl_no, g->flags);
	return 1;
}

void system_enter_psl_mode(void)
{
	/* Configure pins from GPIOs to PSL which rely on VSBY power rail. */
	gpio_config_module(MODULE_PMU, 1);

	/*
	 * Only PSL_IN events can pull PSL_OUT to high and reboot ec.
	 * We should treat it as wake-up pin reset.
	 */
	NPCX_BBRAM(BBRM_DATA_INDEX_WAKE) = HIBERNATE_WAKE_PIN;

	/*
	 * Pull PSL_OUT (GPIO85) to low to cut off ec's VCC power rail by
	 * setting bit 5 of PDOUT(8).
	 */
	SET_BIT(NPCX_PDOUT(GPIO_PORT_8), 5);
}

/* Hibernate function implemented by PSL (Power Switch Logic) mode. */
__noreturn void __keep __enter_hibernate_in_psl(void)
{
	system_enter_psl_mode();
	/* Spin and wait for PSL cuts power; should never return */
	while (1)
		;
}
void board_hibernate_late(void)
{
	int i;

	/*
	 * If the SKU cannot use PSL hibernate, immediately return to go the
	 * non-PSL hibernate flow.
	 */
	if (!is_psl_hibernate) {
		NPCX_KBSINPU = 0x0A;
		return;
	}

	for (i = 0; i < hibernate_wake_pins_used; i++) {
		/* Config PSL pins setting for wake-up inputs */
		if (!system_config_psl_mode(hibernate_wake_pins[i]))
			ccprintf("Invalid PSL setting in wake-up pin %d\n", i);
	}

	/* Clear all pending IRQ otherwise wfi will have no affect */
	for (i = NPCX_IRQ_0; i < NPCX_IRQ_COUNT; i++)
		task_clear_pending_irq(i);

	__enter_hibernate_in_psl();
}

/* This callback disables keyboard when convertibles are fully open */
__override void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;

	if (board_is_convertible())
		keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}

int board_is_lid_angle_tablet_mode(void)
{
	return board_is_convertible();
}

/* Battery functions */
#define SB_OPTIONALMFG_FUNCTION2 0x3e
/* Optional mfg function2 */
#define SMART_QUICK_CHARGE (1 << 12)
/* Quick charge support */
#define MODE_QUICK_CHARGE_SUPPORT (1 << 4)

static void sb_quick_charge_mode(int enable)
{
	int val, rv;

	rv = sb_read(SB_BATTERY_MODE, &val);
	if (rv || !(val & MODE_QUICK_CHARGE_SUPPORT))
		return;

	rv = sb_read(SB_OPTIONALMFG_FUNCTION2, &val);
	if (rv)
		return;

	if (enable)
		val |= SMART_QUICK_CHARGE;
	else
		val &= ~SMART_QUICK_CHARGE;

	sb_write(SB_OPTIONALMFG_FUNCTION2, val);
}

/* Called on AP S3/S0ix -> S0 transition */
static void board_chipset_resume(void)
{
	/* Normal charge current */
	sb_quick_charge_mode(0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3/S0ix transition */
static void board_chipset_suspend(void)
{
	/* Quick charge current */
	sb_quick_charge_mode(1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;

	/* Note that the level is inverted because the pin is active low. */
	gpio_set_level(GPIO_USB_C_OC, !is_overcurrented);
}

static const struct ppc_config_t ppc_syv682x_port0 = {
	.i2c_port = I2C_PORT_TCPC0,
	.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
	.drv = &syv682x_drv,
};

static const struct ppc_config_t ppc_syv682x_port1 = {
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = SYV682X_ADDR0_FLAGS,
	.drv = &syv682x_drv,
};

static void board_setup_ppc(void)
{
	if (!support_syv_ppc)
		return;

	memcpy(&ppc_chips[USB_PD_PORT_TCPC_0], &ppc_syv682x_port0,
	       sizeof(struct ppc_config_t));
	memcpy(&ppc_chips[USB_PD_PORT_TCPC_1], &ppc_syv682x_port1,
	       sizeof(struct ppc_config_t));

	gpio_set_flags(GPIO_USB_PD_C0_INT_ODL, GPIO_INT_BOTH);
	gpio_set_flags(GPIO_USB_PD_C1_INT_ODL, GPIO_INT_BOTH);
}
DECLARE_HOOK(HOOK_INIT, board_setup_ppc, HOOK_PRIO_INIT_I2C + 2);

int ppc_get_alert_status(int port)
{
	if (port == 0)
		return gpio_get_level(GPIO_USB_PD_C0_INT_ODL) == 0;

	return gpio_get_level(GPIO_USB_PD_C1_INT_ODL) == 0;
}
