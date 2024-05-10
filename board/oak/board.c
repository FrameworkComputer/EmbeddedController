/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Oak board configuration */

#include "adc.h"
#include "als.h"
#include "atomic.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_opt3001.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/tcpci.h"
#include "driver/temp_sensor/tmp432.h"
#include "driver/usb_mux/pi3usb3x532.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Dispaly port hardware can connect to port 0, 1 or neither. */
#define PD_PORT_NONE -1

void pd_mcu_interrupt(enum gpio_signal signal)
{
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
}

#if BOARD_REV >= OAK_REV4
void usb_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_INTR);
	usb_charger_task_set_event(1, USB_CHG_EVENT_INTR);
}
#endif /* BOARD_REV >= OAK_REV4 */

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_SOC_POWER_GOOD, POWER_SIGNAL_ACTIVE_HIGH, "POWER_GOOD" },
	{ GPIO_SUSPEND_L, POWER_SIGNAL_ACTIVE_LOW, "SUSPEND#_ASSERTED" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/*
	 * PSYS_MONITOR(PA2): ADC_IN2, 1.44 uA/W on 6.05k Ohm
	 * output in mW
	 */
	[ADC_PSYS] = { "PSYS", 379415, 4096, 0, STM32_AIN(2) },
	/* AMON_BMON(PC0): ADC_IN10, output in uV */
	[ADC_AMON_BMON] = { "AMON_BMON", 183333, 4096, 0, STM32_AIN(10) },
	/* VDC_BOOSTIN_SENSE(PC1): ADC_IN11, output in mV */
	[ADC_VBUS] = { "VBUS", 33000, 4096, 0, STM32_AIN(11) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = { { .name = "battery",
					  .port = I2C_PORT_BATTERY,
					  .kbps = 100,
					  .scl = GPIO_I2C0_SCL,
					  .sda = GPIO_I2C0_SDA },
					{ .name = "pd",
					  .port = I2C_PORT_PD_MCU,
					  .kbps = 1000,
					  .scl = GPIO_I2C1_SCL,
					  .sda = GPIO_I2C1_SDA } };

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef CONFIG_ACCELGYRO_BMI160
/* SPI devices */
const struct spi_device_t spi_devices[] = { { CONFIG_SPI_ACCEL_PORT, 1,
					      GPIO_SPI2_NSS } };
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);
#endif

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC,
			.addr_flags = CONFIG_TCPC_I2C_BASE_ADDR_FLAGS,
		},
		.drv = &tcpci_tcpm_drv,
	},
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC,
			.addr_flags = CONFIG_TCPC_I2C_BASE_ADDR_FLAGS + 1,
		},
		.drv = &tcpci_tcpm_drv,
	},
};

struct mutex pericom_mux_lock;
struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_gpio = GPIO_USB_C_BC12_SEL,
		.mux_gpio_level = 0,
		.mux_lock = &pericom_mux_lock,
	},
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_gpio = GPIO_USB_C_BC12_SEL,
		.mux_gpio_level = 1,
		.mux_lock = &pericom_mux_lock,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

#if BOARD_REV == OAK_REV1
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = I2C_ADDR_CHARGER_FLAGS,
		.drv = &bq2477x_drv,
	},
};
#else
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
#endif /* OAK_REV1 */

/*
 * Temperature sensors data; must be in same order as enum temp_sensor_id.
 * Sensor index and name must match those present in coreboot:
 *     src/mainboard/google/${board}/acpi/dptf.asl
 */
const struct temp_sensor_t temp_sensors[] = {
	{ "TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
	  TMP432_IDX_LOCAL },
	{ "TMP432_Sensor_1", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
	  TMP432_IDX_REMOTE1 },
	{ "TMP432_Sensor_2", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
	  TMP432_IDX_REMOTE2 },
	{ "Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

#ifdef HAS_TASK_ALS
/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{ "TI", opt3001_init, opt3001_read_lux, 5 },
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);
#endif

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
				.driver = &pi3usb3x532_usb_mux_driver,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 1,
				.i2c_port = I2C_PORT_USB_MUX,
#if (BOARD_REV <= OAK_REV4)
				.i2c_addr_flags = PI3USB3X532_I2C_ADDR1,
				.driver = &pi3usb3x532_usb_mux_driver,
#else
				.i2c_addr_flags = 0x10,
				.driver = &ps8740_usb_mux_driver,
#endif
			},
	},
};

/**
 * Store the current DP hardware route.
 */
static int dp_hw_port = PD_PORT_NONE;
static struct mutex dp_hw_lock;

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	crec_usleep(100);
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
}

/**
 * There is a level shift for AC_OK & LID_OPEN signal between AP & EC,
 * disable it (drive high) when AP is off, otherwise enable it (drive low).
 */
static void board_extpower_buffer_to_soc(void)
{
	/* Drive high when AP is off (G3), else drive low */
	gpio_set_level(GPIO_LEVEL_SHIFT_EN_L,
		       chipset_in_state(CHIPSET_STATE_HARD_OFF) ? 1 : 0);
}

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Assert wake GPIO to PD MCU to wake it from hibernate.
	 * This cannot be done from board_pre_init() (or from any function
	 * called before system_pre_init()), otherwise a spurious wake will
	 * occur -- see stm32 check_reset_cause() WORKAROUND comment.
	 */
	gpio_set_level(GPIO_USB_PD_VBUS_WAKE, 1);

	/* Enable Level shift of AC_OK & LID_OPEN signals */
	board_extpower_buffer_to_soc();
	/* Enable rev1 testing GPIOs */
	gpio_set_level(GPIO_SYSTEM_POWER_H, 1);
	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);

#if BOARD_REV >= OAK_REV4
	/* Enable BC 1.2 interrupt */
	gpio_enable_interrupt(GPIO_USB_BC12_INT);
#endif /* BOARD_REV >= OAK_REV4 */

#if BOARD_REV >= OAK_REV3
	/* Update VBUS supplier */
	usb_charger_vbus_change(0, !gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L));
	usb_charger_vbus_change(1, !gpio_get_level(GPIO_USB_C1_VBUS_WAKE_L));
#else
	usb_charger_vbus_change(0, 0);
	usb_charger_vbus_change(1, 0);
#endif

#ifdef CONFIG_ACCELGYRO_BMI160
	/* SPI sensors: put back the GPIO in its expected state */
	gpio_set_level(GPIO_SPI2_NSS, 1);

	/* Remap SPI2 to DMA channels 6 and 7 (0011) */
	STM32_DMA_CSELR(STM32_DMAC_CH6) |= (3 << 20) | (3 << 24);

	/* Enable SPI for BMI160 */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);

	/* Set all four SPI pins to high speed */
	/* pins D0/D1/D3/D4 */
	STM32_GPIO_OSPEEDR(GPIO_D) |= 0x000003cf;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(&spi_devices[0], 1);
	CPRINTS("Board using SPI sensors");
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Set active charge port -- only one port can active at a time.
 *
 * @param charge_port    Charge port to enable.
 *
 * Return EC_SUCCESS if charge port is accepted and made active.
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_MAX_COUNT);
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
					     GPIO_USB_C1_CHARGE_L,
			       1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_L :
					     GPIO_USB_C0_CHARGE_L,
			       0);
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
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	charge_set_input_current_limit(charge_ma, charge_mv);
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

static void board_typec_set_dp_hpd(int port, int level)
{
#if BOARD_REV >= OAK_REV5
	if (1 == dp_hw_port)
		gpio_set_level(GPIO_C1_DP_HPD, level);
#endif

	gpio_set_level(GPIO_USB_DP_HPD, level);
}

/**
 * Turn on DP hardware on type-C port.
 */
void board_typec_dp_on(int port)
{
	mutex_lock(&dp_hw_lock);

	if (dp_hw_port != !port) {
		/* Get control of DP hardware */
		dp_hw_port = port;
#if BOARD_REV == OAK_REV2 || BOARD_REV >= OAK_REV5
		/* Rev2 or Rev5 later board has DP switch */
		gpio_set_level(GPIO_DP_SWITCH_CTL, port);
#endif
		if (!gpio_get_level(GPIO_USB_DP_HPD)) {
			board_typec_set_dp_hpd(port, 1);
		} else {
			uint64_t now = get_time().val;
			/* wait for the minimum spacing between IRQ_HPD */
			if (now < hpd_deadline[port])
				crec_usleep(hpd_deadline[port] - now);

			board_typec_set_dp_hpd(port, 0);
			crec_usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
			board_typec_set_dp_hpd(port, 1);
		}
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;

	mutex_unlock(&dp_hw_lock);
}

/**
 * Turn off a PD port's DP output.
 */
void board_typec_dp_off(int port, int *dp_flags)
{
	mutex_lock(&dp_hw_lock);

	if (dp_hw_port == !port) {
		mutex_unlock(&dp_hw_lock);
		return;
	}

	dp_hw_port = PD_PORT_NONE;
	board_typec_set_dp_hpd(port, 0);

	mutex_unlock(&dp_hw_lock);

	/* Enable the other port if its dp flag is on */
	if (dp_flags[!port] & DP_FLAGS_DP_ON)
		board_typec_dp_on(!port);
}

/**
 * Set DP hotplug detect level.
 */
void board_typec_dp_set(int port, int level)
{
	mutex_lock(&dp_hw_lock);

	if (dp_hw_port == PD_PORT_NONE) {
		dp_hw_port = port;
#if BOARD_REV == OAK_REV2 || BOARD_REV >= OAK_REV5
		/* Rev2 or Rev5 later board has DP switch */
		gpio_set_level(GPIO_DP_SWITCH_CTL, port);
#endif
	}

	if (dp_hw_port == port)
		board_typec_set_dp_hpd(port, level);

	mutex_unlock(&dp_hw_lock);
}

#if BOARD_REV < OAK_REV3
#ifndef CONFIG_AP_WARM_RESET_INTERRUPT
/* Using this hook if system doesn't have enough external line. */
static void check_ap_reset_second(void)
{
	/* Check the warm reset signal from servo board */
	static int warm_reset, last;

	warm_reset = !gpio_get_level(GPIO_AP_RESET_L);

	if (last == warm_reset)
		return;

	if (warm_reset)
		chipset_reset(); /* Warm reset AP */

	last = warm_reset;
}
DECLARE_HOOK(HOOK_SECOND, check_ap_reset_second, HOOK_PRIO_DEFAULT);
#endif
#endif

/**
 * Set AP reset.
 *
 * PMIC_WARM_RESET_H (PB3) is connected to PMIC RESET before rev < 3.
 * AP_RESET_L (PC3, CPU_WARM_RESET_L) is connected to PMIC SYSRSTB
 * after rev >= 3.
 */
void board_set_ap_reset(int asserted)
{
	if (system_get_board_version() < 3) {
		/* Signal is active-high */
		CPRINTS("pmic warm reset(%d)", asserted);
		gpio_set_level(GPIO_PMIC_WARM_RESET_H, asserted);
	} else {
		/* Signal is active-low */
		CPRINTS("ap warm reset(%d)", asserted);
		gpio_set_level(GPIO_AP_RESET_L, !asserted);
	}
}

#if BOARD_REV < OAK_REV4
/**
 * Check VBUS state and trigger USB BC1.2 charger.
 */
void vbus_task(void *u)
{
	struct {
		uint8_t interrupt;
		uint8_t device_type;
		uint8_t charger_status;
		uint8_t vbus;
	} bc12[CONFIG_USB_PD_PORT_MAX_COUNT];
	uint8_t port, vbus, reg, wake;

	while (1) {
		for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
#if BOARD_REV == OAK_REV3
			vbus = !gpio_get_level(port ? GPIO_USB_C1_VBUS_WAKE_L :
						      GPIO_USB_C0_VBUS_WAKE_L);
#else
			vbus = tcpm_check_vbus_level(port, VBUS_PRESENT);
#endif
			/* check if VBUS changed */
			if (((bc12[port].vbus >> port) & 1) == vbus)
				continue;
			/* wait 1.2 seconds and check BC 1.2 status */
			crec_msleep(1200);

			if (vbus)
				bc12[port].vbus |= 1 << port;
			else
				bc12[port].vbus &= ~BIT(port);

			wake = 0;
			reg = pi3usb9281_get_interrupts(port);
			if (reg != bc12[port].interrupt) {
				bc12[port].interrupt = reg;
				wake++;
			}

			reg = pi3usb9281_get_device_type(port);
			if (reg != bc12[port].device_type) {
				bc12[port].device_type = reg;
				wake++;
			}

			reg = pi3usb9281_get_charger_status(port);
			if (reg != bc12[port].charger_status) {
				bc12[port].charger_status = reg;
				wake++;
			}

			if (wake)
				usb_charger_task_set_event(port,
							   USB_CHG_EVENT_BC12);
		}
		task_wait_event(-1);
	}
}
#else
void vbus_task(void *u)
{
	while (1)
		task_wait_event(-1);
}
#endif /* BOARD_REV < OAK_REV4 */

#ifdef CONFIG_TEMP_SENSOR_TMP432
static void tmp432_set_power_deferred(void)
{
	/* Shut tmp432 down if not in S0 && no external power */
	if (!extpower_is_present() && !chipset_in_state(CHIPSET_STATE_ON)) {
		if (EC_SUCCESS != tmp432_set_power(TMP432_POWER_OFF))
			CPRINTS("ERROR: Can't shutdown TMP432.");
		return;
	}

	/*  else, turn it on. */
	if (EC_SUCCESS != tmp432_set_power(TMP432_POWER_ON))
		CPRINTS("ERROR: Can't turn on TMP432.");
}
DECLARE_DEFERRED(tmp432_set_power_deferred);
#endif

/**
 * Hook of AC change. turn on/off tmp432 depends on AP & AC status.
 */
static void board_extpower(void)
{
	board_extpower_buffer_to_soc();
#ifdef CONFIG_TEMP_SENSOR_TMP432
	hook_call_deferred(&tmp432_set_power_deferred_data, 0);
#endif
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/* Called on AP S5 -> S3 transition, and before HOOK_CHIPSET_STARTUP */
static void board_chipset_pre_init(void)
{
	/* Enable level shift of AC_OK when power on */
	board_extpower_buffer_to_soc();
#if BOARD_REV >= OAK_REV5
	/* Enable DP muxer */
	gpio_set_level(GPIO_DP_MUX_EN_L, 0);
	gpio_set_level(GPIO_PARADE_MUX_EN, 1);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable level shift to SoC when shutting down */
	gpio_set_level(GPIO_LEVEL_SHIFT_EN_L, 1);
#if BOARD_REV >= OAK_REV5
	/* Disable DP muxer */
	gpio_set_level(GPIO_DP_MUX_EN_L, 1);
	gpio_set_level(GPIO_PARADE_MUX_EN, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void board_chipset_resume(void)
{
#ifdef CONFIG_TEMP_SENSOR_TMP432
	hook_call_deferred(&tmp432_set_power_deferred_data, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
#ifdef CONFIG_TEMP_SENSOR_TMP432
	hook_call_deferred(&tmp432_set_power_deferred_data, 0);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

#ifdef HAS_TASK_MOTIONSENSE
/* Motion sensors */
/* Mutexes */
#ifdef CONFIG_ACCEL_KX022
static struct mutex g_lid_mutex;
#endif
#ifdef CONFIG_ACCELGYRO_BMI160
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
				       { 0, FLOAT_TO_FP(-1), 0 },
				       { 0, 0, FLOAT_TO_FP(1) } };
#endif

static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;

struct motion_sensor_t motion_sensors[] = {
#ifdef CONFIG_ACCELGYRO_BMI160
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(0),
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
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ACCEL_MK_SPI_ADDR_FLAGS(0),
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
#endif
#ifdef CONFIG_ACCEL_KX022
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = NULL, /* Identity matrix. */
		.default_range = 2, /* g, to support lid angle calculation. */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
#endif
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

__override void lid_angle_peripheral_enable(int enable)
{
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif /* defined(HAS_TASK_MOTIONSENSE) */
