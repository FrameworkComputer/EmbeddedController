/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Hammer board configuration */

#include "charger.h"
#include "clock.h"
#include "common.h"
#include "driver/charger/isl923x.h"
#include "driver/led/lm3630a.h"
#include "ec_ec_comm_server.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "printf.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "queue.h"
#include "queue_policies.h"
#include "registers.h"
#include "rollback.h"
#include "spi.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "touchpad.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "usb_api.h"
#include "usb_descriptor.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#ifdef SECTION_IS_RW
#define CROS_EC_SECTION "RW"
#else
#define CROS_EC_SECTION "RO"
#endif

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Hammer"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] =
		USB_STRING_DESC(CROS_EC_SECTION ":" CROS_EC_VERSION32),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("SPI"),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
#ifdef CONFIG_USB_ISOCHRONOUS
	[USB_STR_HEATMAP_NAME] = USB_STRING_DESC("Heatmap"),
#endif
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support I2C bridging over USB.
 */

#ifdef SECTION_IS_RW
#ifdef HAS_SPI_TOUCHPAD
/* SPI devices */
const struct spi_device_t spi_devices[] = {
	[SPI_ST_TP_DEVICE_ID] = { CONFIG_SPI_TOUCHPAD_PORT, 2, GPIO_SPI1_NSS,
				  USB_SPI_ENABLED },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/* SPI interface is always enabled, no need to do anything. */
void usb_spi_board_enable(void)
{
}
void usb_spi_board_disable(void)
{
}
#endif /* !HAS_SPI_TOUCHPAD */

#ifdef CONFIG_I2C
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "master",
	  .port = I2C_PORT_MASTER,
	  .kbps = 400,
	  .scl = GPIO_MASTER_I2C_SCL,
	  .sda = GPIO_MASTER_I2C_SDA },
#ifdef BOARD_WAND
	{ .name = "charger",
	  .port = I2C_PORT_CHARGER,
	  .kbps = 100,
	  .scl = GPIO_CHARGER_I2C_SCL,
	  .sda = GPIO_CHARGER_I2C_SDA },
#endif
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
#endif

#ifdef CONFIG_CHARGER_ISL9238
const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

#endif

#ifdef HAS_BACKLIGHT
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{ STM32_TIM(TIM_KBLIGHT), STM32_TIM_CH(1), 0, KBLIGHT_PWM_FREQ },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
#endif /* HAS_BACKLIGHT */

int usb_i2c_board_is_enabled(void)
{
	/* Disable I2C passthrough when the system is locked */
	return !system_is_locked();
}

__override struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 50,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
#endif

#if defined(BOARD_WAND) && defined(SECTION_IS_RW)
struct consumer const ec_ec_usart_consumer;
static struct usart_config const ec_ec_usart;

struct queue const ec_ec_comm_server_input =
	QUEUE_DIRECT(64, uint8_t, ec_ec_usart.producer, ec_ec_usart_consumer);
struct queue const ec_ec_comm_server_output =
	QUEUE_DIRECT(64, uint8_t, null_producer, ec_ec_usart.consumer);

struct consumer const ec_ec_usart_consumer = {
	.queue = &ec_ec_comm_server_input,
	.ops = &((struct consumer_ops const){
		.written = ec_ec_comm_server_written,
	}),
};

static struct usart_config const ec_ec_usart =
	USART_CONFIG(EC_EC_UART, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     USART_CONFIG_FLAG_HDSEL, ec_ec_comm_server_input,
		     ec_ec_comm_server_output);
#endif /* BOARD_WAND && SECTION_IS_RW */

/******************************************************************************
 * Initialize board.
 */
static int has_keyboard_backlight;

#ifdef SECTION_IS_RW
static void board_init(void)
{
#ifdef HAS_BACKLIGHT
	/* Detect keyboard backlight: pull-down means it is present. */
	has_keyboard_backlight = !gpio_get_level(GPIO_KEYBOARD_BACKLIGHT);

	CPRINTS("Backlight%s present", has_keyboard_backlight ? "" : " not");
#endif /* HAS_BACKLIGHT */

#ifdef BOARD_WAND
	/* USB to serial queues */
	queue_init(&ec_ec_comm_server_input);
	queue_init(&ec_ec_comm_server_output);

	/* UART init */
	usart_init(&ec_ec_usart);
#endif /* BOARD_WAND */

#ifdef CONFIG_LED_DRIVER_LM3630A
	lm3630a_poweron();
#endif

#ifdef HAS_SPI_TOUCHPAD
	spi_enable(&spi_devices[SPI_ST_TP_DEVICE_ID], 0);

	/* Disable SPI passthrough when the system is locked */
	usb_spi_enable(system_is_locked());

	/* Set all four SPI pins to high speed */
	/* pins B3/5, A15 */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0x00000cc0;
	STM32_GPIO_OSPEEDR(GPIO_A) |= 0xc0000000;

	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= STM32_RCC_PB2_SPI1;
	STM32_RCC_APB2RSTR &= ~STM32_RCC_PB2_SPI1;
	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_PB2_SPI1;

	clock_wait_bus_cycles(BUS_APB, 1);
	/* Enable SPI for touchpad */
	gpio_config_module(MODULE_SPI_CONTROLLER, 1);
	spi_enable(&spi_devices[SPI_ST_TP_DEVICE_ID], 1);
#endif /* HAS_SPI_TOUCHPAD */
}
/* This needs to happen before PWM is initialized. */
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_INIT_PWM - 1);
#endif /* SECTION_IS_RW */

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);

	/* Remap USART DMA to match the USART driver */
	/*
	 * the DMA mapping is :
	 *  Chan 4 : USART1_TX
	 *  Chan 5 : USART1_RX
	 */
	STM32_SYSCFG_CFGR1 |= BIT(9) | BIT(10); /* Remap USART1 RX/TX DMA */
}

int board_has_keyboard_backlight(void)
{
	return has_keyboard_backlight;
}

#ifndef HAS_NO_TOUCHPAD
/* Reset the touchpad, mainly used to recover it from malfunction. */
void board_touchpad_reset(void)
{
#ifdef HAS_EN_PP3300_TP_ACTIVE_HIGH
	gpio_set_level(GPIO_EN_PP3300_TP, 0);
	crec_msleep(100);
	gpio_set_level(GPIO_EN_PP3300_TP, 1);
	crec_msleep(100);
#else
	gpio_set_level(GPIO_EN_PP3300_TP_ODL, 1);
	crec_msleep(10);
	gpio_set_level(GPIO_EN_PP3300_TP_ODL, 0);
	crec_msleep(10);
#endif
}
#endif /* !HAS_NO_TOUCHPAD */

#ifdef CONFIG_KEYBOARD_TABLET_MODE_SWITCH
static void board_tablet_mode_change(void)
{
	/*
	 * Turn off key scanning in tablet mode.
	 */
	if (tablet_get_mode())
		keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	else
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, board_tablet_mode_change,
	     HOOK_PRIO_DEFAULT);
/* Run after tablet_mode_init. */
DECLARE_HOOK(HOOK_INIT, board_tablet_mode_change, HOOK_PRIO_DEFAULT + 1);
#endif

/*
 * Get entropy based on Clock Recovery System, which is enabled on hammer to
 * synchronize USB SOF with internal oscillator.
 */
int board_get_entropy(void *buffer, int len)
{
	int i = 0;
	uint8_t *data = buffer;
	uint32_t start;
	/* We expect one SOF per ms, so wait at most 2ms. */
	const uint32_t timeout = 2 * MSEC;

	for (i = 0; i < len; i++) {
		STM32_CRS_ICR |= STM32_CRS_ICR_SYNCOKC;
		start = __hw_clock_source_read();
		while (!(STM32_CRS_ISR & STM32_CRS_ISR_SYNCOKF)) {
			if ((__hw_clock_source_read() - start) > timeout)
				return 0;
			crec_usleep(500);
		}
		/* Pick 8 bits, including FEDIR and 7 LSB of FECAP. */
		data[i] = STM32_CRS_ISR >> 15;
	}

	return 1;
}

/*
 * Generate a USB serial number from unique chip ID.
 */
__override const char *board_read_serial(void)
{
	static char str[CONFIG_SERIALNO_LEN];

	if (str[0] == '\0') {
		uint8_t *id;
		int pos = 0;
		int idlen = system_get_chip_unique_id(&id);
		int i;

		for (i = 0; i < idlen && pos < sizeof(str); i++, pos += 2) {
			if (snprintf(&str[pos], sizeof(str) - pos, "%02x",
				     id[i]) < 0)
				return NULL;
		}
	}

	return str;
}

__override int board_write_serial(const char *serialno)
{
	return 0;
}

static const struct ec_response_keybd_config zed_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,
		TK_REFRESH,
		TK_FULLSCREEN,
		TK_OVERVIEW,
		TK_SNAPSHOT,
		TK_BRIGHTNESS_DOWN,
		TK_BRIGHTNESS_UP,
		TK_VOL_MUTE,
		TK_VOL_DOWN,
		TK_VOL_UP,
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config bland_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,
		TK_REFRESH,
		TK_FULLSCREEN,
		TK_OVERVIEW,
		TK_BRIGHTNESS_DOWN,
		TK_BRIGHTNESS_UP,
		TK_MICMUTE,
		TK_VOL_MUTE,
		TK_VOL_DOWN,
		TK_VOL_UP,
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config duck_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,
		TK_FORWARD,
		TK_REFRESH,
		TK_FULLSCREEN,
		TK_OVERVIEW,
		TK_BRIGHTNESS_DOWN,
		TK_BRIGHTNESS_UP,
		TK_VOL_MUTE,
		TK_VOL_DOWN,
		TK_VOL_UP,
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (IS_ENABLED(BOARD_ZED) || IS_ENABLED(BOARD_STAR) ||
	    IS_ENABLED(BOARD_GELATIN))
		return &zed_kb;
	if (IS_ENABLED(BOARD_BLAND) || IS_ENABLED(BOARD_EEL))
		return &bland_kb;
	if (IS_ENABLED(BOARD_DUCK))
		return &duck_kb;

	return NULL;
}
