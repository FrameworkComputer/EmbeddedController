/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo V4 configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)


/******************************************************************************
 * Build GPIO tables and expose a subset of the GPIOs over USB.
 */

#include "gpio_list.h"

static enum gpio_signal const usb_gpio_list[] = {
/* Outputs */
GPIO_DUT_CHG_EN,		/* 0 */
GPIO_HOST_OR_CHG_CTL,
GPIO_DP_HPD,
GPIO_SBU_UART_SEL,
GPIO_HOST_USB_HUB_RESET_L,
GPIO_FASTBOOT_DUTHUB_MUX_SEL,	/* 5 */
GPIO_SBU_MUX_EN,
GPIO_FASTBOOT_DUTHUB_MUX_EN_L,
GPIO_DUT_HUB_USB_RESET_L,
GPIO_ATMEL_HWB_L,
GPIO_CMUX_EN,			/* 10 */
GPIO_EMMC_MUX_EN_L,
GPIO_EMMC_PWR_EN,


/* Inputs */
GPIO_USERVO_FAULT_L,
GPIO_USB_FAULT_L,
GPIO_DONGLE_DET,		/* 15 */

GPIO_USB_DET_PP_DUT,
GPIO_USB_DET_PP_CHG,

GPIO_USB_DUT_CC2_RPUSB,
GPIO_USB_DUT_CC2_RD,
GPIO_USB_DUT_CC2_RA,		/* 20 */
GPIO_USB_DUT_CC1_RP3A0,
GPIO_USB_DUT_CC1_RP1A5,
GPIO_USB_DUT_CC1_RPUSB,
GPIO_USB_DUT_CC1_RD,
GPIO_USB_DUT_CC1_RA,		/* 25 */
GPIO_USB_DUT_CC2_RP3A0,
GPIO_USB_DUT_CC2_RP1A5,

};

/*
 * This instantiates struct usb_gpio_config const usb_gpio, plus several other
 * variables, all named something beginning with usb_gpio_
 */
USB_GPIO_CONFIG(usb_gpio,
		usb_gpio_list,
		USB_IFACE_GPIO,
		USB_EP_GPIO);


/******************************************************************************
 * Set up USB PD
 */

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_DUT_CC1_PD] = {"DUT_CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_DUT_CC2_PD] = {"DUT_CC2_PD", 3300, 4096, 0, STM32_AIN(5)},
	[ADC_CHG_CC1_PD] = {"CHG_CC1_PD", 3300, 4096, 0, STM32_AIN(2)},
	[ADC_CHG_CC2_PD] = {"CHG_CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
	[ADC_SBU1_DET] = {"SBU1_DET", 3300, 4096, 0, STM32_AIN(3)},
	[ADC_SBU2_DET] = {"SBU2_DET", 3300, 4096, 0, STM32_AIN(7)},
	[ADC_SUB_C_REF] = {"SUB_C_REF", 3300, 4096, 0, STM32_AIN(1)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);


/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE	16
#define USB_STREAM_TX_SIZE	16

/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 = QUEUE_DIRECT(64, uint8_t,
	usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		115200,
		usart3_to_usb,
		usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb,
	USB_IFACE_USART3_STREAM,
	USB_STR_USART3_STREAM_NAME,
	USB_EP_USART3_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart3,
	usart3_to_usb)


/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 = QUEUE_DIRECT(64, uint8_t,
	usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		9600,
		usart4_to_usb,
		usb_to_usart4);

USB_STREAM_CONFIG(usart4_usb,
	USB_IFACE_USART4_STREAM,
	USB_STR_USART4_STREAM_NAME,
	USB_EP_USART4_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart4,
	usart4_to_usb)


/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Servo V4"),
	[USB_STR_SERIALNO]     = USB_STRING_DESC("1234-a"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_I2C_NAME]     = USB_STRING_DESC("I2C"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo EC Shell"),
	[USB_STR_USART3_STREAM_NAME]  = USB_STRING_DESC("DUT UART"),
	[USB_STR_USART4_STREAM_NAME]  = USB_STRING_DESC("Atmega UART"),
	[USB_STR_UPDATE_NAME]  = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);



/******************************************************************************
 * Support I2C bridging over USB, this requires usb_i2c_board_enable and
 * usb_i2c_board_disable to be defined to enable and disable the SPI bridge.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_enable(void) {return EC_SUCCESS; }
void usb_i2c_board_disable(int debounce) {}


/******************************************************************************
 * Support firmware upgrade over USB. We can update whichever section is not
 * the current section.
 */

/*
 * This array defines possible sections available for the firmware update.
 * The section which does not map the current executing code is picked as the
 * valid update area. The values are offsets into the flash space.
 */
const struct section_descriptor board_rw_sections[] = {
	{CONFIG_RO_MEM_OFF,
	 CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE},
	{CONFIG_RW_MEM_OFF,
	 CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE},
};
const struct section_descriptor * const rw_sections = board_rw_sections;
const int num_rw_sections = ARRAY_SIZE(board_rw_sections);


/******************************************************************************
 * Initialize board.
 */

/* Write a GPIO output on the tca6416 I2C ioexpander. */
static void write_ioexpander(int bank, int gpio, int val)
{
	int tmp;

	/* Read output port register */
	i2c_read8(1, 0x40, 0x2 + bank, &tmp);
	if (val)
		tmp |= (1 << gpio);
	else
		tmp &= ~(1 << gpio);
	/* Write back modified output port register */
	i2c_write8(1, 0x40, 0x2 + bank, tmp);

	/* Set Configuration port to output/0 */
	i2c_read8(1, 0x40, 0x6 + bank, &tmp);
	i2c_write8(1, 0x40, 0x6 + bank, tmp & ~(1 << gpio));
}

/* Enable uservo USB. */
static void init_uservo_port(void)
{
	/* Write USERVO_POWER_EN */
	write_ioexpander(0, 7, 1);
	/* Write USERVO_FASTBOOT_MUX_SEL */
	write_ioexpander(1, 0, 0);
}

/* Enable all ioexpander outputs. */
static void init_ioexpander(void)
{
	/* Write all GPIO to output 0 */
	i2c_write8(1, 0x40, 0x2, 0x0);
	i2c_write8(1, 0x40, 0x3, 0x0);
	/* Write all GPIO to output direction */
	i2c_write8(1, 0x40, 0x6, 0x0);
	i2c_write8(1, 0x40, 0x7, 0x0);
}

/* State of CC lines presented to DUT */
/* Dual Rd pulldown, classic debug device. */
#define CCD_ID_RDRD	0
/* RpUSB + Rp1A5, indicates a self powered dongle. */
#define CCD_ID_RPUSB	1
/* One Rd. Device w/o CCD */
#define CCD_ID_NONE	4

static int ccd_id = CCD_ID_NONE;

/* Set CC values according to requested mode. */
static void init_ccd(int mode)
{
	int cc1_rd = GPIO_INPUT;
	int cc2_rd = GPIO_INPUT;
	int cc1_rpusb = GPIO_INPUT;
	int cc2_rpusb = GPIO_INPUT;
	int cc1_rp1a5 = GPIO_INPUT;
	int cc2_rp1a5 = GPIO_INPUT;
	int cc1_rp3a0 = GPIO_INPUT;
	int cc2_rp3a0 = GPIO_INPUT;

	switch (mode) {
	case CCD_ID_RDRD:
		cc1_rd = GPIO_OUT_LOW;
		cc2_rd = GPIO_OUT_LOW;
		break;

	case CCD_ID_RPUSB:
		cc1_rpusb = GPIO_OUT_HIGH;
		cc2_rp1a5 = GPIO_OUT_HIGH;
		break;

	default:
		cc1_rd = GPIO_OUT_LOW;
		mode = CCD_ID_NONE;
		break;
	}

	gpio_set_flags(GPIO_USB_DUT_CC1_RD, cc1_rd);
	gpio_set_flags(GPIO_USB_DUT_CC2_RD, cc2_rd);
	gpio_set_flags(GPIO_USB_DUT_CC1_RPUSB, cc1_rpusb);
	gpio_set_flags(GPIO_USB_DUT_CC2_RPUSB, cc2_rpusb);
	gpio_set_flags(GPIO_USB_DUT_CC1_RP1A5, cc1_rp1a5);
	gpio_set_flags(GPIO_USB_DUT_CC2_RP1A5, cc2_rp1a5);
	gpio_set_flags(GPIO_USB_DUT_CC1_RP3A0, cc1_rp3a0);
	gpio_set_flags(GPIO_USB_DUT_CC2_RP3A0, cc2_rp3a0);

	/* Disable CCD until we can detect orientation */
	gpio_set_level(GPIO_SBU_MUX_EN, 0);
	write_ioexpander(0, 0, 0);

	ccd_id = mode;
}


/* Define voltage thresholds for CCD and SBU USB detection */
#define GND_MAX_MV	350
#define PULL_0V35_MV	350
#define PULL_0V55_MV	550
#define PULL_0V9_MV	900
#define PULL_1V1_MV	1100
#define PULL_2V0_MV	2000
#define POWER_MIN_MV	3000
#define USB_HIGH_MV	1500

/* Check if presented CCD was accepted by the device */
static int check_ccd_request(int cc1, int cc2)
{
	if ((ccd_id == CCD_ID_RDRD) &&
	    (cc1 > PULL_0V35_MV) && (cc1 < PULL_0V55_MV) &&
	    (cc2 > PULL_0V35_MV) && (cc2 < PULL_0V55_MV))
		return 1;

	if ((ccd_id == CCD_ID_RPUSB) &&
	    (cc1 > PULL_0V35_MV) && (cc1 < PULL_0V55_MV) &&
	    (cc2 > PULL_0V9_MV) && (cc2 < PULL_1V1_MV))
		return 1;

	return 0;
}

/* Check if CC lines indicate an unplug event */
static int check_usb_disconnect(int cc1, int cc2)
{
	if ((cc1 < GND_MAX_MV) && (cc2 < GND_MAX_MV))
		return 1;

	if ((cc1 > POWER_MIN_MV) && (cc2 > POWER_MIN_MV))
		return 1;

	return 0;
}


/* Current mode for CCD USB line connection */
/* Cable not plugged in */
#define CCD_MODE_DISCONNECTED	0
/* Cable plugged in, CCD detected in default orientation */
#define CCD_MODE_CONNECTED	1
/* Cable plugged in, CCD detected in flip orientation */
#define CCD_MODE_CONNECTED_FLIP	2
/* Cable plugged in, nothing detected on SBU lines */
#define CCD_MODE_CONNECTED_NONE	3
/* No type-c cable in servo. */
#define CCD_MODE_USBA		4

static int mode = CCD_MODE_DISCONNECTED;

/*
 * We don't have an available interrupt, so we'll just check this
 * every second. Update state every tick if necessary.
 */
static void usb_sbu_tick(void)
{
	int cc1, cc2;

	/* Check if we have a CCD cable */
	if ((mode == CCD_MODE_USBA) || !gpio_get_level(GPIO_DONGLE_DET)) {
		mode = CCD_MODE_USBA;
		return;
	}

	/* Check CC lines via ADC */
	cc1 = adc_read_channel(ADC_DUT_CC1_PD);
	cc2 = adc_read_channel(ADC_DUT_CC2_PD);

	if (mode == CCD_MODE_DISCONNECTED) {
		/* Check if both CC lines are pulled, and we are connected */
		if (check_ccd_request(cc1, cc2)) {
			int sbu1;
			int sbu2;

			/*
			 * Give the onboard CCD micro 100ms
			 * to notice and enable USB, then check adc levels.
			 */
			usleep(100000);
			sbu1 = adc_read_channel(ADC_SBU1_DET);
			sbu2 = adc_read_channel(ADC_SBU2_DET);

			CPRINTS("CCD: Plug detect cc1:%d cc2:%d "
				"sbu1:%d, sbu2:%d",
				cc1, cc2, sbu1, sbu2);

			/* USB FS pulls one line high for connect request */
			if ((sbu1 > USB_HIGH_MV) && (sbu2 < GND_MAX_MV)) {
				/* SBU flip = 1 */
				write_ioexpander(0, 2, 1);
				usleep(10000);
				gpio_set_level(GPIO_SBU_MUX_EN, 1);
				mode = CCD_MODE_CONNECTED;
				CPRINTS("CCD: connected flip");
			} else if ((sbu2 > USB_HIGH_MV) &&
				   (sbu1 < GND_MAX_MV)) {
				/* SBU flip = 0 */
				write_ioexpander(0, 2, 0);
				usleep(10000);
				gpio_set_level(GPIO_SBU_MUX_EN, 1);
				mode = CCD_MODE_CONNECTED_FLIP;
				CPRINTS("CCD: connected noflip");
			} else {
				mode = CCD_MODE_CONNECTED_NONE;
				CPRINTS("CCD: connected none");
			}
		}
	} else {
		/* mode == CCD_MODE_CONNECTED[_FLIP] */
		if (check_usb_disconnect(cc1, cc2)) {
			/* We are not connected to anything */

			/* Turn off CCD */
			gpio_set_level(GPIO_SBU_MUX_EN, 0);
			CPRINTS("CCD: disconnect");
			mode = CCD_MODE_DISCONNECTED;
		}
	}
}
DECLARE_HOOK(HOOK_TICK, usb_sbu_tick, HOOK_PRIO_DEFAULT);


static void board_init(void)
{
	/* USB to serial queues */
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);

	/* UART init */
	usart_init(&usart3);
	usart_init(&usart4);

	/* Delay DUT hub to avoid brownout. */
	usleep(1000);
	gpio_set_flags(GPIO_DUT_HUB_USB_RESET_L, GPIO_OUT_HIGH);

	/* Write USB3 Mode Enable to PS8742 USB/DP Mux. */
	i2c_write8(1, 0x20, 0x0, 0x20);

	/* Enable uservo USB by default. */
	init_ioexpander();
	init_uservo_port();

	/* Enable CCD if type-c */
	if (gpio_get_level(GPIO_DONGLE_DET))
		init_ccd(CCD_ID_RPUSB);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static int command_ccd(int argc, char **argv)
{
	int mode = CCD_ID_NONE;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Handle requested mode */
	if (!strcasecmp(argv[1], "rdrd"))
		mode = CCD_ID_RDRD;
	else if (!strcasecmp(argv[1], "rpusb"))
		mode = CCD_ID_RPUSB;
	else if (!strcasecmp(argv[1], "off"))
		mode = CCD_ID_NONE;
	else
		return EC_ERROR_PARAM1;

	init_ccd(mode);
	ccprintf("DUT CC lines set to %s\n", argv[1]);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ccd, command_ccd,
	"[rdrd|rpusb|off]", "Set pullups or pulldowns to indicate CCD");
