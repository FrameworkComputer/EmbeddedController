/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ryu board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "atomic.h"
#include "battery.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usb.h"
#include "usb_pd.h"
#include "usb_spi.h"
#include "usb-stm32f3.h"
#include "usb-stream.h"
#include "usart-stm32f3.h"
#include "util.h"
#include "pi3usb9281.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Default input current limit when VBUS is present */
#define DEFAULT_CURR_LIMIT            500  /* mA */

/* VBUS too low threshold */
#define VBUS_LOW_THRESHOLD_MV 4600

/* Input current error margin */
#define IADP_ERROR_MARGIN_MA 100

static int charge_current_limit;

/*
 * PD host event status for host command
 * Note: this variable must be aligned on 4-byte boundary because we pass the
 * address to atomic_ functions which use assembly to access them.
 */
static struct ec_response_host_event_status host_event_status __aligned(4);

/*
 * Store the state of our USB data switches so that they can be restored
 * after pericom reset.
 */
static int usb_switch_state;

static void vbus_log(void)
{
	CPRINTS("VBUS %d", gpio_get_level(GPIO_CHGR_ACOK));
}
DECLARE_DEFERRED(vbus_log);

void vbus_evt(enum gpio_signal signal)
{
	struct charge_port_info charge;
	int vbus_level = gpio_get_level(signal);

	/*
	 * If VBUS is low, or VBUS is high and we are not outputting VBUS
	 * ourselves, then update the VBUS supplier.
	 */
	if (!vbus_level || !gpio_get_level(GPIO_CHGR_OTG)) {
		charge.voltage = USB_BC12_CHARGE_VOLTAGE;
		charge.current = vbus_level ? DEFAULT_CURR_LIMIT : 0;
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0, &charge);
	}

	hook_call_deferred(vbus_log, 0);
	if (task_start_called())
		task_wake(TASK_ID_PD);
}

/* Wait 200ms after a charger is detected to debounce pin contact order */
#define USB_CHG_DEBOUNCE_DELAY_MS 200
/*
 * Wait 100ms after reset, before re-enabling attach interrupt, so that the
 * spurious attach interrupt from certain ports is ignored.
 */
#define USB_CHG_RESET_DELAY_MS 100

void usb_charger_task(void)
{
	int device_type, charger_status;
	struct charge_port_info charge;
	int type;
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;

	while (1) {
		/* Read interrupt register to clear */
		pi3usb9281_get_interrupts(0);

		/* Set device type */
		device_type = pi3usb9281_get_device_type(0);
		charger_status = pi3usb9281_get_charger_status(0);

		/* Debounce pin plug order if we detect a charger */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			msleep(USB_CHG_DEBOUNCE_DELAY_MS);

			/* Trigger chip reset to refresh detection registers */
			pi3usb9281_reset(0);
			/*
			 * Restore data switch settings - switches return to
			 * closed on reset until restored.
			 */
			if (usb_switch_state)
				pi3usb9281_set_switches(0, 1);

			/* Clear possible disconnect interrupt */
			pi3usb9281_get_interrupts(0);
			/* Mask attach interrupt */
			pi3usb9281_set_interrupt_mask(0,
						      0xff &
						      ~PI3USB9281_INT_ATTACH);
			/* Re-enable interrupts */
			pi3usb9281_enable_interrupts(0);
			msleep(USB_CHG_RESET_DELAY_MS);

			/* Clear possible attach interrupt */
			pi3usb9281_get_interrupts(0);
			/* Re-enable attach interrupt */
			pi3usb9281_set_interrupt_mask(0, 0xff);

			/* Re-read ID registers */
			device_type = pi3usb9281_get_device_type(0);
			charger_status = pi3usb9281_get_charger_status(0);
		}

		if (PI3USB9281_CHG_STATUS_ANY(charger_status))
			type = CHARGE_SUPPLIER_PROPRIETARY;
		else if (device_type & PI3USB9281_TYPE_CDP)
			type = CHARGE_SUPPLIER_BC12_CDP;
		else if (device_type & PI3USB9281_TYPE_DCP)
			type = CHARGE_SUPPLIER_BC12_DCP;
		else if (device_type & PI3USB9281_TYPE_SDP)
			type = CHARGE_SUPPLIER_BC12_SDP;
		else
			type = CHARGE_SUPPLIER_OTHER;

		/* Attachment: decode + update available charge */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			charge.current = pi3usb9281_get_ilim(device_type,
							     charger_status);
			charge_manager_update_charge(type, 0, &charge);
		} else { /* Detachment: update available charge to 0 */
			charge.current = 0;
			charge_manager_update_charge(
						CHARGE_SUPPLIER_PROPRIETARY,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_BC12_CDP,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_BC12_DCP,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_BC12_SDP,
						0,
						&charge);
			charge_manager_update_charge(
						CHARGE_SUPPLIER_OTHER,
						0,
						&charge);
		}

		/* notify host of power info change */
		pd_send_host_event(PD_EVENT_POWER_CHANGE);

		/* Wait for interrupt */
		task_wait_event(-1);
	}
}

void usb_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
}

#include "gpio_list.h"

const void *const usb_strings[] = {
	[USB_STR_DESC]           = usb_string_desc,
	[USB_STR_VENDOR]         = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]        = USB_STRING_DESC("Ryu debug"),
	[USB_STR_VERSION]        = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME]   = USB_STRING_DESC("EC_PD"),
	[USB_STR_AP_STREAM_NAME] = USB_STRING_DESC("AP"),
	[USB_STR_SH_STREAM_NAME] = USB_STRING_DESC("SH"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/*
 * Define AP and SH console forwarding queues and associated USART and USB
 * stream endpoints.
 */
struct usart_config const ap_usart;
struct usart_config const sh_usart;

struct usb_stream_config const ap_usb;
struct usb_stream_config const sh_usb;

static struct queue const ap_usart_to_usb = QUEUE_DIRECT(64, uint8_t,
							 ap_usart.producer,
							 ap_usb.consumer);
static struct queue const ap_usb_to_usart = QUEUE_DIRECT(64, uint8_t,
							 ap_usb.producer,
							 ap_usart.consumer);
static struct queue const sh_usart_to_usb = QUEUE_DIRECT(64, uint8_t,
							 sh_usart.producer,
							 sh_usb.consumer);
static struct queue const sh_usb_to_usart = QUEUE_DIRECT(64, uint8_t,
							 sh_usb.producer,
							 sh_usart.consumer);

USART_CONFIG(ap_usart, usart1_hw, 115200, ap_usart_to_usb, ap_usb_to_usart)
USART_CONFIG(sh_usart, usart3_hw, 115200, sh_usart_to_usb, sh_usb_to_usart)

#define AP_USB_STREAM_RX_SIZE	16
#define AP_USB_STREAM_TX_SIZE	16

USB_STREAM_CONFIG(ap_usb,
		  USB_IFACE_AP_STREAM,
		  USB_STR_AP_STREAM_NAME,
		  USB_EP_AP_STREAM,
		  AP_USB_STREAM_RX_SIZE,
		  AP_USB_STREAM_TX_SIZE,
		  ap_usb_to_usart,
		  ap_usart_to_usb)

#define SH_USB_STREAM_RX_SIZE	16
#define SH_USB_STREAM_TX_SIZE	16

USB_STREAM_CONFIG(sh_usb,
		  USB_IFACE_SH_STREAM,
		  USB_STR_SH_STREAM_NAME,
		  USB_EP_SH_STREAM,
		  SH_USB_STREAM_RX_SIZE,
		  SH_USB_STREAM_TX_SIZE,
		  sh_usb_to_usart,
		  sh_usart_to_usb)

/* Initialize board. */
static void board_init(void)
{
	struct charge_port_info charge_none, charge_vbus;

	/* Initialize all pericom charge suppliers to 0 */
	charge_none.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge_none.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
				     0,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, 0, &charge_none);

	/* Initialize VBUS supplier based on whether or not VBUS is present */
	charge_vbus.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge_vbus.current = DEFAULT_CURR_LIMIT;
	if (gpio_get_level(GPIO_CHGR_ACOK))
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_vbus);
	else
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_none);

	/* Enable pericom BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USBC_BC12_INT_L);
	pi3usb9281_set_interrupt_mask(0, 0xff);
	pi3usb9281_enable_interrupts(0);

	/*
	 * Determine recovery mode is requested by the power, volup, and
	 * voldown buttons being pressed.
	 */
	if (power_button_signal_asserted() &&
	    !gpio_get_level(GPIO_BTN_VOLD_L) &&
	    !gpio_get_level(GPIO_BTN_VOLU_L))
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	/*
	 * Initialize AP and SH console forwarding USARTs and queues.
	 */
	queue_init(&ap_usart_to_usb);
	queue_init(&ap_usb_to_usart);
	queue_init(&sh_usart_to_usb);
	queue_init(&sh_usb_to_usart);
	usart_init(&ap_usart);
	usart_init(&sh_usart);

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_DEVICE_ODL lines are
	 * set low to specify device mode.
	 */
	gpio_set_level(GPIO_USBC_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_CHGR_ACOK);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_HOLD, 1, "AP_HOLD"},
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, /10 voltage divider. */
	[ADC_VBUS] = {"VBUS",  30000, 4096, 0, STM32_AIN(0)},
	/* USB PD CC lines sensing. Converted to mV (3000mV/4096). */
	[ADC_CC1_PD] = {"CC1_PD", 3000, 4096, 0, STM32_AIN(1)},
	[ADC_CC2_PD] = {"CC2_PD", 3000, 4096, 0, STM32_AIN(3)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Charge supplier priority: lower number indicates higher priority. */
const int supplier_priority[] = {
	[CHARGE_SUPPLIER_PD] = 0,
	[CHARGE_SUPPLIER_TYPEC] = 1,
	[CHARGE_SUPPLIER_PROPRIETARY] = 1,
	[CHARGE_SUPPLIER_BC12_DCP] = 1,
	[CHARGE_SUPPLIER_BC12_CDP] = 2,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_OTHER] = 3,
	[CHARGE_SUPPLIER_VBUS] = 4
};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Sensor mutex */
static struct mutex g_mutex;

/*  local sensor data (per-sensor) */
struct bmi160_drv_data_t g_bmi160_data;

struct motion_sensor_t motion_sensors[] = {

	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	{.name = "Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_mutex,
	 .drv_data = &g_bmi160_data,
	 .i2c_addr = BMI160_ADDR0,
	 .rot_standard_ref = NULL,
	 .default_config = {
		 .odr = 100000,
		 .range = 2
	 }
	},

	{.name = "Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_mutex,
	 .drv_data = &g_bmi160_data,
	 .i2c_addr = BMI160_ADDR0,
	 .rot_standard_ref = NULL,
	 .default_config = {
		 .odr = 100000,
		 .range = 2000
	 }
	},

	{.name = "Mag",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_MAG,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_mutex,
	 .drv_data = &g_bmi160_data,
	 .i2c_addr = BMI160_ADDR0,
	 .rot_standard_ref = NULL,
	 .default_config = {
		 .odr = 25000,
		 .range = 1
	 }
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void board_set_usb_switches(int port, int open)
{
	/* If switch is not changing, then return */
	if (open == usb_switch_state)
		return;

	usb_switch_state = open;
	pi3usb9281_set_switches(port, open);
}

void board_set_usb_mux(int port, enum typec_mux mux,
		       enum usb_switch usb, int polarity)
{
	/* reset everything */
	gpio_set_level(GPIO_USBC_MUX_CONF0, 0);
	gpio_set_level(GPIO_USBC_MUX_CONF1, 0);
	gpio_set_level(GPIO_USBC_MUX_CONF2, 0);

	/* Set D+/D- switch to appropriate level */
	board_set_usb_switches(port, usb);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	gpio_set_level(GPIO_USBC_MUX_CONF0, polarity);

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK)
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(GPIO_USBC_MUX_CONF2, 1);

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK)
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_MUX_CONF1, 1);
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_usb, has_dp, polarity;

	has_usb = gpio_get_level(GPIO_USBC_MUX_CONF2);
	has_dp = gpio_get_level(GPIO_USBC_MUX_CONF1);
	polarity = gpio_get_level(GPIO_USBC_MUX_CONF0);

	if (has_dp)
		*dp_str = polarity ? "DP2" : "DP1";
	else
		*dp_str = NULL;

	if (has_usb)
		*usb_str = polarity ? "USB2" : "USB1";
	else
		*usb_str = NULL;

	return has_dp || has_usb;
}

void board_flip_usb_mux(int port)
{
	int has_usb, has_dp, polarity;
	enum typec_mux mux;

	has_usb = gpio_get_level(GPIO_USBC_MUX_CONF2);
	has_dp = gpio_get_level(GPIO_USBC_MUX_CONF1);
	polarity = gpio_get_level(GPIO_USBC_MUX_CONF0);
	mux = has_usb && has_dp ? TYPEC_MUX_DOCK :
		(has_dp ? TYPEC_MUX_DP :
		(has_usb ? TYPEC_MUX_USB : TYPEC_MUX_NONE));

	board_set_usb_mux(port, mux, usb_switch_state, !polarity);
}

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

void usb_board_connect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 0);
}

void usb_board_disconnect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 1);
}

/* Charge manager callback function, called on delayed override timeout */
void board_charge_manager_override_timeout(void)
{
	/* TODO: Implement me! */
}
DECLARE_DEFERRED(board_charge_manager_override_timeout);

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
	/* check if we are source vbus on that port */
	int src = gpio_get_level(GPIO_CHGR_OTG);

	if (charge_port >= 0 && charge_port < CONFIG_USB_PD_PORT_COUNT && src) {
		CPRINTS("Port %d is not a sink, skipping enable", charge_port);
		return EC_ERROR_INVAL;
	}

	/* Enable/disable charging */
	gpio_set_level(GPIO_USBC_CHARGE_EN_L, charge_port == CHARGE_PORT_NONE);

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	int rv;

	charge_current_limit = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);
	rv = charge_set_input_current_limit(charge_current_limit);
	if (rv < 0)
		CPRINTS("Failed to set input current limit for PD");
}

/* Send host event up to AP */
void pd_send_host_event(int mask)
{
	/* mask must be set */
	if (!mask)
		return;

	atomic_or(&(host_event_status.status), mask);
	/* interrupt the AP */
	host_set_single_event(EC_HOST_EVENT_PD_MCU);
}

/**
 * Enable and disable SPI for case closed debugging.  This forces the AP into
 * reset while SPI is enabled, thus preventing contention on the SPI interface.
 */
void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* Place AP into reset */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, 0);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 1);
	gpio_set_flags(GPIO_SPI_FLASH_NSS, GPIO_OUT_HIGH);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xf03c0000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	/* Enable SPI LDO to power the flash chip */
	gpio_set_level(GPIO_VDDSPI_EN, 1);

	spi_enable(1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	spi_enable(0);

	/* Disable SPI LDO */
	gpio_set_level(GPIO_VDDSPI_EN, 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_MASTER, 0);
	gpio_set_flags(GPIO_SPI_FLASH_NSS, GPIO_INPUT);

	/* Release AP from reset */
	gpio_set_level(GPIO_PMIC_WARM_RESET_L, 1);
}

int board_get_version(void)
{
	static int ver;

	if (!ver) {
		/*
		 * read the board EC ID on the tristate strappings
		 * using ternary encoding: 0 = 0, 1 = 1, Hi-Z = 2
		 */
		uint8_t id0 = 0, id1 = 0;
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_PULL_DOWN | GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_PULL_DOWN | GPIO_INPUT);
		usleep(100);
		id0 = gpio_get_level(GPIO_BOARD_ID0);
		id1 = gpio_get_level(GPIO_BOARD_ID1);
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_PULL_UP | GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_PULL_UP | GPIO_INPUT);
		usleep(100);
		id0 = gpio_get_level(GPIO_BOARD_ID0) && !id0 ? 2 : id0;
		id1 = gpio_get_level(GPIO_BOARD_ID1) && !id1 ? 2 : id1;
		gpio_set_flags(GPIO_BOARD_ID0, GPIO_INPUT);
		gpio_set_flags(GPIO_BOARD_ID1, GPIO_INPUT);
		ver = id1 * 3 + id0;
		CPRINTS("Board ID = %d\n", ver);
	}

	return ver;
}

/****************************************************************************/
/* Host commands */

static int host_event_status_host_cmd(struct host_cmd_handler_args *args)
{
	struct ec_response_host_event_status *r = args->response;

	/* Read and clear the host event status to return to AP */
	r->status = atomic_read_clear(&(host_event_status.status));

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_HOST_EVENT_STATUS, host_event_status_host_cmd,
			EC_VER_MASK(0));
