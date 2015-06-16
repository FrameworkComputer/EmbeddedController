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
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usb.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_spi.h"
#include "usb-stm32f3.h"
#include "usb-stream.h"
#include "usart-stm32f3.h"
#include "util.h"
#include "pi3usb9281.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

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
	if (!vbus_level || !gpio_get_level(GPIO_USBC_5V_EN)) {
		charge.voltage = USB_CHARGER_VOLTAGE_MV;
		charge.current = vbus_level ? USB_CHARGER_MIN_CURR_MA : 0;
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0, &charge);
	}

	hook_call_deferred(vbus_log, 0);
	if (task_start_called())
		task_wake(TASK_ID_PD);
}

void usb_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG_P0);
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
static struct usart_config const ap_usart;
static struct usart_config const sh_usart;

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

static struct usart_config const ap_usart = USART_CONFIG(usart1_hw,
							 usart_rx_interrupt,
							 usart_tx_interrupt,
							 115200,
							 ap_usart_to_usb,
							 ap_usb_to_usart);

static struct usart_config const sh_usart = USART_CONFIG(usart3_hw,
							 usart_rx_interrupt,
							 usart_tx_interrupt,
							 115200,
							 sh_usart_to_usb,
							 sh_usb_to_usart);

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

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_PERICOM,
		.mux_lock = NULL,
	}
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver    = &p5_board_custom_usb_mux_driver,
	},
};

/* Initialize board. */
static void board_init(void)
{
	struct charge_port_info charge_none, charge_vbus;

	/* Select P4 driver for old boards due to different GPIO config */
	if (board_get_version() < 5)
		usb_muxes[0].driver = &p4_board_custom_usb_mux_driver;

	/* Initialize all pericom charge suppliers to 0 */
	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
				     0,
				     &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP, 0, &charge_none);
	charge_manager_update_charge(CHARGE_SUPPLIER_OTHER, 0, &charge_none);

	/* Initialize VBUS supplier based on whether or not VBUS is present */
	charge_vbus.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_vbus.current = USB_CHARGER_MIN_CURR_MA;
	if (gpio_get_level(GPIO_CHGR_ACOK))
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_vbus);
	else
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_none);

	/* Enable pericom BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USBC_BC12_INT_L);

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

	/*
	 * TODO(crosbug.com/p/38689) Workaround for PMIC issue on P5.
	 * remove when P5 are de-commissioned.
	 * We are re-using EXTINT1 for the new power sequencing workaround
	 * this is killing the base closing detection on P5
	 * we won't charge it.
	 */
	if (board_get_version() == 5)
		gpio_enable_interrupt(GPIO_HPD_IN);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_HOLD, 1, "AP_HOLD"},
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/*
 * TODO(crosbug.com/p/38689) Workaround for MAX77620 PMIC EN_PP3300 issue.
 * remove when P5 are de-commissioned.
 */
void pp1800_on_off_evt(enum gpio_signal signal)
{
	int level = gpio_get_level(signal);
	gpio_set_level(GPIO_EN_PP3300_RSVD, level);
}

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, /10 voltage divider. */
	[ADC_VBUS] = {"VBUS",  30000, 4096, 0, STM32_AIN(0)},
	/* USB PD CC lines sensing. Converted to mV (3000mV/4096). */
	[ADC_CC1_PD] = {"CC1_PD", 3000, 4096, 0, STM32_AIN(1)},
	[ADC_CC2_PD] = {"CC2_PD", 3000, 4096, 0, STM32_AIN(3)},
	/* Charger current sensing. Converted to mA. */
	[ADC_IADP] = {"IADP",  7500, 4096, 0, STM32_AIN(8)},
	[ADC_IBAT] = {"IBAT", 37500, 4096, 0, STM32_AIN(13)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void board_set_usb_switches(int port, enum usb_switch setting)
{
	/* If switch is not changing, then return */
	if (setting == usb_switch_state)
		return;

	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state = setting;
	pi3usb9281_set_switches(port, usb_switch_state);
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
	int ret = EC_SUCCESS;
	/* check if we are source vbus on that port */
	int src = gpio_get_level(GPIO_USBC_5V_EN);

	if (charge_port >= 0 && charge_port < CONFIG_USB_PD_PORT_COUNT && src) {
		CPRINTS("Port %d is not a sink, skipping enable", charge_port);
		charge_port = CHARGE_PORT_NONE;
		ret = EC_ERROR_INVAL;
	}
	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable charging */
		charge_set_input_current_limit(0);
	}

	return ret;
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
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP ||
	       supplier == CHARGE_SUPPLIER_BC12_SDP ||
	       supplier == CHARGE_SUPPLIER_BC12_CDP ||
	       supplier == CHARGE_SUPPLIER_PROPRIETARY;
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

/**
 * Return if board is consuming full amount of input current
 */
int board_is_consuming_full_charge(void)
{
	return adc_read_channel(ADC_IADP) >= charge_current_limit -
					     IADP_ERROR_MARGIN_MA;
}

/**
 * Return if VBUS is sagging low enough that we should stop ramping
 */
int board_is_vbus_too_low(enum chg_ramp_vbus_state ramp_state)
{
	return adc_read_channel(ADC_VBUS) < VBUS_LOW_THRESHOLD_MV;
}

/*
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
