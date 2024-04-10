/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Tigertail board configuration */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ina2xx.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "usb-stream.h"
#include "usb_i2c.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE 16
#define USB_STREAM_TX_SIZE 16

/******************************************************************************
 * Forward USART1 as a simple USB serial interface.
 */
static struct usart_config const usart1;
struct usb_stream_config const usart1_usb;

static struct queue const usart1_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart1.producer, usart1_usb.consumer);
static struct queue const usb_to_usart1 =
	QUEUE_DIRECT(64, uint8_t, usart1_usb.producer, usart1.consumer);

static struct usart_config const usart1 =
	USART_CONFIG(usart1_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart1_to_usb, usb_to_usart1);

USB_STREAM_CONFIG(usart1_usb, USB_IFACE_USART1_STREAM,
		  USB_STR_USART1_STREAM_NAME, USB_EP_USART1_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart1,
		  usart1_to_usb)

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Tigertail"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
	[USB_STR_USART1_STREAM_NAME] = USB_STRING_DESC("DUT UART"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Tigertail Console"),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * ADC support for SBU flip detect.
 */
/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_SBU1] = { "SBU1", 3300, 4096, 0, STM32_AIN(6) },
	[ADC_SBU2] = { "SBU2", 3300, 4096, 0, STM32_AIN(7) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************
 * Support I2C bridging over USB.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "master",
	  .port = I2C_PORT_MASTER,
	  .kbps = 100,
	  .scl = GPIO_MASTER_I2C_SCL,
	  .sda = GPIO_MASTER_I2C_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/******************************************************************************
 * Console commands.
 */

/* State to indicate current GPIO config. */
static int uart_state = UART_OFF;
/* State to indicate current autodetect mode. */
static int uart_detect = UART_DETECT_AUTO;

const char *const uart_state_names[] = {
	[UART_OFF] = "off",
	[UART_ON_PP1800] = "on @ 1.8v",
	[UART_FLIP_PP1800] = "flip @ 1.8v",
	[UART_ON_PP3300] = "on @ 3.3v",
	[UART_FLIP_PP3300] = "flip @ 3.3v",
	[UART_AUTO] = "auto",
};

/* Set GPIOs to configure UART mode. */
static void set_uart_gpios(int state)
{
	int uart = GPIO_INPUT;
	int dir = 0;
	int voltage = 1; /* 1: 1.8v, 0: 3.3v */
	int enabled = 0;

	gpio_set_level(GPIO_ST_UART_LVL_DIS, 1);

	switch (state) {
	case UART_ON_PP1800:
		uart = GPIO_ALTERNATE;
		dir = 1;
		voltage = 1;
		enabled = 1;
		break;

	case UART_FLIP_PP1800:
		uart = GPIO_ALTERNATE;
		dir = 0;
		voltage = 1;
		enabled = 1;
		break;

	case UART_ON_PP3300:
		uart = GPIO_ALTERNATE;
		dir = 1;
		voltage = 0;
		enabled = 1;
		break;

	case UART_FLIP_PP3300:
		uart = GPIO_ALTERNATE;
		dir = 0;
		voltage = 0;
		enabled = 1;
		break;

	default:
		/* Default to UART_OFF. */
		uart = GPIO_INPUT;
		dir = 0;
		enabled = 0;
	}

	/* Set level shifter direction and voltage. */
	gpio_set_level(GPIO_ST_UART_VREF, voltage);
	gpio_set_level(GPIO_ST_UART_TX_DIR, dir);
	gpio_set_level(GPIO_ST_UART_TX_DIR_N, !dir);

	/* Enable STM pinmux */
	gpio_set_flags(GPIO_USART1_TX, uart);
	gpio_set_flags(GPIO_USART1_RX, uart);

	/* Flip uart orientation if necessary. */
	STM32_USART_CR1(STM32_USART1_BASE) &= ~(STM32_USART_CR1_UE);
	if (dir)
		STM32_USART_CR2(STM32_USART1_BASE) &= ~(STM32_USART_CR2_SWAP);
	else
		STM32_USART_CR2(STM32_USART1_BASE) |= (STM32_USART_CR2_SWAP);
	STM32_USART_CR1(STM32_USART1_BASE) |= STM32_USART_CR1_UE;

	/* Enable level shifter. */
	crec_usleep(1000);
	gpio_set_level(GPIO_ST_UART_LVL_DIS, !enabled);
}

/*
 * Detect if a UART is plugged into SBU. Tigertail UART must be off
 * for this to return useful info.
 */
static int is_low(int mv)
{
	return (mv < 190);
}

static int is_3300(int mv)
{
	return ((mv > 3000) && (mv < 3400));
}

static int is_1800(int mv)
{
	return ((mv > 1600) && (mv < 1900));
}

static int detect_uart_orientation(void)
{
	int sbu1 = adc_read_channel(ADC_SBU1);
	int sbu2 = adc_read_channel(ADC_SBU2);
	int state = UART_OFF;

	/*
	 * Here we check if one or the other SBU is 1.8v, as DUT
	 * TX should idle high.
	 */
	if (is_low(sbu1) && is_1800(sbu2))
		state = UART_ON_PP1800;
	else if (is_low(sbu2) && is_1800(sbu1))
		state = UART_FLIP_PP1800;
	else if (is_low(sbu1) && is_3300(sbu2))
		state = UART_ON_PP3300;
	else if (is_low(sbu2) && is_3300(sbu1))
		state = UART_FLIP_PP3300;
	else
		state = UART_OFF;

	return state;
}

/*
 * Detect if UART has been unplugged. Normal UARTs should
 * have both lines idling high at 1.8v.
 */
static int detect_uart_idle(void)
{
	int sbu1 = adc_read_channel(ADC_SBU1);
	int sbu2 = adc_read_channel(ADC_SBU2);
	int enabled = 0;

	if (is_1800(sbu1) && is_1800(sbu2))
		enabled = 1;

	if (is_3300(sbu1) && is_3300(sbu2))
		enabled = 1;

	return enabled;
}

/* Set the UART state and gpios, and autodetect if necessary. */
void set_uart_state(int state)
{
	if (state == UART_AUTO) {
		set_uart_gpios(UART_OFF);
		crec_msleep(10);

		uart_detect = UART_DETECT_AUTO;
		state = detect_uart_orientation();
	} else {
		uart_detect = UART_DETECT_OFF;
	}

	uart_state = state;
	set_uart_gpios(state);
}

/*
 * Autodetect UART state:
 * We will check every 250ms, and change state if 1 second has passed
 * in the new state.
 */
void uart_sbu_tick(void)
{
	static int debounce; /* = 0 */

	if (uart_detect != UART_DETECT_AUTO)
		return;

	if (uart_state == UART_OFF) {
		int state = detect_uart_orientation();

		if (state != UART_OFF) {
			debounce++;
			if (debounce > 4) {
				debounce = 0;
				CPRINTS("UART autoenable %s",
					uart_state_names[state]);
				uart_state = state;
				set_uart_gpios(state);
			}
			return;
		}
	} else {
		int enabled = detect_uart_idle();

		if (!enabled) {
			debounce++;
			if (debounce > 4) {
				debounce = 0;
				CPRINTS("UART autodisable");
				uart_state = UART_OFF;
				set_uart_gpios(UART_OFF);
			}
			return;
		}
	}
	debounce = 0;
}
DECLARE_HOOK(HOOK_TICK, uart_sbu_tick, HOOK_PRIO_DEFAULT);

static int command_uart(int argc, const char **argv)
{
	const char *uart_state_str = "off";
	const char *uart_detect_str = "manual";

	if (argc > 1) {
		if (!strcasecmp("off", argv[1]))
			set_uart_state(UART_OFF);
		else if (!strcasecmp("on18", argv[1]))
			set_uart_state(UART_ON_PP1800);
		else if (!strcasecmp("on33", argv[1]))
			set_uart_state(UART_ON_PP3300);
		else if (!strcasecmp("flip18", argv[1]))
			set_uart_state(UART_FLIP_PP1800);
		else if (!strcasecmp("flip33", argv[1]))
			set_uart_state(UART_FLIP_PP3300);
		else if (!strcasecmp("auto", argv[1]))
			set_uart_state(UART_AUTO);
		else
			return EC_ERROR_PARAM1;
	}

	uart_state_str = uart_state_names[uart_state];
	if (uart_detect == UART_DETECT_AUTO)
		uart_detect_str = "auto";
	ccprintf("UART mux is: %s, setting: %s\n", uart_state_str,
		 uart_detect_str);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(uart, command_uart,
			"[off|on18|on33|flip18|flip33|auto]",
			"Set the sbu uart state\n"
			"WARNING: 3.3v may damage 1.8v devices.\n");

static void set_led_a(int r, int g, int b)
{
	/* LEDs are active low */
	gpio_set_level(GPIO_LED_R_L, !r);
	gpio_set_level(GPIO_LED_G_L, !g);
	gpio_set_level(GPIO_LED_B_L, !b);
}

static void set_led_b(int r, int g, int b)
{
	gpio_set_level(GPIO_LED2_R_L, !r);
	gpio_set_level(GPIO_LED2_G_L, !g);
	gpio_set_level(GPIO_LED2_B_L, !b);
}

/* State we intend the mux GPIOs to be set. */
static int mux_state = MUX_OFF;
static int last_mux_state = MUX_OFF;

/* Set the state variable and GPIO configs to mux as requested. */
void set_mux_state(int state)
{
	int enabled = (state == MUX_A) || (state == MUX_B);
	/* dir: 0 -> A, dir: 1 -> B */
	int dir = (state == MUX_B);

	if (mux_state != state)
		last_mux_state = mux_state;

	/* Disconnect first. */
	gpio_set_level(GPIO_USB_C_OE_N, 1);
	gpio_set_level(GPIO_SEL_RELAY_A, 0);
	gpio_set_level(GPIO_SEL_RELAY_B, 0);

	/* Let USB disconnect. */
	crec_msleep(100);

	/* Reconnect VBUS/CC in the requested direction. */
	gpio_set_level(GPIO_SEL_RELAY_A, !dir && enabled);
	gpio_set_level(GPIO_SEL_RELAY_B, dir && enabled);

	/* Reconnect data. */
	crec_msleep(10);

	gpio_set_level(GPIO_USB_C_SEL_B, dir);
	gpio_set_level(GPIO_USB_C_OE_N, !enabled);

	if (!enabled)
		mux_state = MUX_OFF;
	else
		mux_state = state;

	if (state == MUX_A)
		set_led_a(0, 1, 0);
	else
		set_led_a(1, 0, 0);

	if (state == MUX_B)
		set_led_b(0, 1, 0);
	else
		set_led_b(1, 0, 0);
}

/* On button press, toggle between mux A, B, off. */
static int button_ready = 1;
void button_interrupt_deferred(void)
{
	switch (mux_state) {
	case MUX_OFF:
		if (last_mux_state == MUX_A)
			set_mux_state(MUX_B);
		else
			set_mux_state(MUX_A);
		break;

	case MUX_A:
	case MUX_B:
	default:
		set_mux_state(MUX_OFF);
		break;
	}

	button_ready = 1;
}
DECLARE_DEFERRED(button_interrupt_deferred);

/* On button press, toggle between mux A, B, off. */
void button_interrupt(enum gpio_signal signal)
{
	if (!button_ready)
		return;

	button_ready = 0;
	/*
	 * button_ready is not set until set_mux_state completes,
	 * which has ~100ms settle time for the mux, which also
	 * provides for debouncing.
	 */
	hook_call_deferred(&button_interrupt_deferred_data, 0);
}

static int command_mux(int argc, const char **argv)
{
	char *mux_state_str = "off";

	if (argc > 1) {
		if (!strcasecmp("off", argv[1]))
			set_mux_state(MUX_OFF);
		else if (!strcasecmp("a", argv[1]))
			set_mux_state(MUX_A);
		else if (!strcasecmp("b", argv[1]))
			set_mux_state(MUX_B);
		else
			return EC_ERROR_PARAM1;
	}

	if (mux_state == MUX_A)
		mux_state_str = "A";
	if (mux_state == MUX_B)
		mux_state_str = "B";
	ccprintf("TYPE-C mux is %s\n", mux_state_str);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mux, command_mux, "[off|A|B]",
			"Get/set the mux and enable state of the TYPE-C mux");

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	/* USB to serial queues */
	queue_init(&usart1_to_usb);
	queue_init(&usb_to_usart1);

	/* UART init */
	usart_init(&usart1);

	/*
	 * Default to port A, to allow easier charging and
	 * detection of unconfigured devices.
	 */
	set_mux_state(MUX_A);

	/* Note that we can't enable AUTO until after init. */
	set_uart_gpios(UART_OFF);

	/* Calibrate INA0 (VBUS) with 1mA/LSB scale */
	ina2xx_init(0, 0x8000, INA2XX_CALIB_1MA(15 /*mOhm*/));
	ina2xx_init(1, 0x8000, INA2XX_CALIB_1MA(15 /*mOhm*/));
	ina2xx_init(4, 0x8000, INA2XX_CALIB_1MA(15 /*mOhm*/));

	gpio_enable_interrupt(GPIO_BUTTON_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
