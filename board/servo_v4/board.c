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
#include "system.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_pd.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"

/******************************************************************************
 * GPIO interrupt handlers.
 */

static void vbus0_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C0);
}

static void vbus1_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C1);
}

#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/******************************************************************************
 * Board pre-init function.
 */

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);

	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (CHG RX) - Default mapping
	 *  Chan 3 : SPI1_TX   (CHG TX) - Default mapping
	 *  Chan 4 : USART1 TX - Remapped from default Chan 2
	 *  Chan 5 : USART1 RX - Remapped from default Chan 3
	 *  Chan 6 : TIM3_CH1  (DUT RX) - Remapped from default Chan 4
	 *  Chan 7 : SPI2_TX   (DUT TX) - Remapped from default Chan 5
	 *
	 * As described in the comments above, both USART1 TX/RX and DUT Tx/RX
	 * channels must be remapped from the defulat locations. Remapping is
	 * acoomplished by setting the following bits in the STM32_SYSCFG_CFGR1
	 * register. Information about this register and its settings can be
	 * found in section 11.3.7 DMA Request Mapping of the STM RM0091
	 * Reference Manual
	 */
	/* Remap USART1 Tx from DMA channel 2 to channel 4 */
	STM32_SYSCFG_CFGR1 |= BIT(9);
	/* Remap USART1 Rx from DMA channel 3 to channel 5 */
	STM32_SYSCFG_CFGR1 |= BIT(10);
	/* Remap TIM3_CH1 from DMA channel 4 to channel 6 */
	STM32_SYSCFG_CFGR1 |= BIT(30);
	/* Remap SPI2 Tx from DMA channel 5 to channel 7 */
	STM32_SYSCFG_CFGR1 |= BIT(24);
}

/******************************************************************************
 * Set up USB PD
 */

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CHG_CC1_PD] = {"CHG_CC1_PD", 3300, 4096, 0, STM32_AIN(2)},
	[ADC_CHG_CC2_PD] = {"CHG_CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
	[ADC_DUT_CC1_PD] = {"DUT_CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_DUT_CC2_PD] = {"DUT_CC2_PD", 3300, 4096, 0, STM32_AIN(5)},
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
		0,
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
		0,
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
 * Support I2C bridging over USB.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_is_enabled(void) { return 1; }

/******************************************************************************
 * Initialize board.
 */

/*
 * Support tca6416 I2C ioexpander.
 */
#define GPIOX_I2C_ADDR_FLAGS	0x20
#define GPIOX_IN_PORT_A		0x0
#define GPIOX_IN_PORT_B		0x1
#define GPIOX_OUT_PORT_A	0x2
#define GPIOX_OUT_PORT_B	0x3
#define GPIOX_DIR_PORT_A	0x6
#define GPIOX_DIR_PORT_B	0x7


/* Write a GPIO output on the tca6416 I2C ioexpander. */
static void write_ioexpander(int bank, int gpio, int val)
{
	int tmp;

	/* Read output port register */
	i2c_read8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_OUT_PORT_A + bank, &tmp);
	if (val)
		tmp |= BIT(gpio);
	else
		tmp &= ~BIT(gpio);
	/* Write back modified output port register */
	i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_OUT_PORT_A + bank, tmp);
}

/* Read a single GPIO input on the tca6416 I2C ioexpander. */
static int read_ioexpander_bit(int bank, int bit)
{
	int tmp;
	int mask = 1 << bit;

	/* Read input port register */
	i2c_read8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_IN_PORT_A + bank, &tmp);

	return (tmp & mask) >> bit;
}

/* Enable uservo USB. */
static void init_uservo_port(void)
{
	/* Write USERVO_POWER_EN */
	write_ioexpander(0, 7, 1);
	/* Write USERVO_FASTBOOT_MUX_SEL */
	write_ioexpander(1, 0, 0);
}

/* Enable blue USB port to DUT. */
static void init_usb3_port(void)
{
	/* Write USB3.0_TYPEA_MUX_SEL */
	write_ioexpander(0, 3, 1);
	/* Write USB3.0_TYPEA_MUX_EN_L */
	write_ioexpander(0, 4, 0);
	/* Write USB3.0_TYPE_A_PWR_EN */
	write_ioexpander(0, 5, 1);
}

/* Enable all ioexpander outputs. */
static void init_ioexpander(void)
{
	/* Write all GPIO to output 0 */
	i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_OUT_PORT_A, 0x0);
	i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_OUT_PORT_B, 0x0);

	/*
	 * Write GPIO direction: strap resistors to input,
	 * all others to output.
	 */
	i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_DIR_PORT_A, 0x0);
	i2c_write8(1, GPIOX_I2C_ADDR_FLAGS, GPIOX_DIR_PORT_B, 0x18);
}

/*
 * Define voltage thresholds for SBU USB detection.
 *
 * Max observed USB low across sampled systems: 666mV
 * Min observed USB high across sampled systems: 3026mV
 */
#define GND_MAX_MV	700
#define USB_HIGH_MV	2500
#define SBU_DIRECT	0
#define SBU_FLIP	1

#define MODE_SBU_DISCONNECT	0
#define MODE_SBU_CONNECT	1
#define MODE_SBU_FLIP		2
#define MODE_SBU_OTHER		3

static void ccd_measure_sbu(void);
DECLARE_DEFERRED(ccd_measure_sbu);
static void ccd_measure_sbu(void)
{
	int sbu1;
	int sbu2;
	int mux_en;
	static int count /* = 0 */;
	static int last /* = 0 */;
	static int polarity /* = 0 */;

	/* Read sbu voltage levels */
	sbu1 = adc_read_channel(ADC_SBU1_DET);
	sbu2 = adc_read_channel(ADC_SBU2_DET);
	mux_en = gpio_get_level(GPIO_SBU_MUX_EN);

	/*
	 * While SBU_MUX is disabled (SuzyQ unplugged), we'll poll the SBU lines
	 * to check if an idling, unconfigured USB device is present.
	 * USB FS pulls one line high for connect request.
	 * If so, and it persists for 500ms, we'll enable the SuzyQ in that
	 * orientation.
	 */
	if ((!mux_en) && (sbu1 > USB_HIGH_MV) && (sbu2 < GND_MAX_MV)) {
		/* Check flip connection polarity. */
		if (last != MODE_SBU_FLIP) {
			last = MODE_SBU_FLIP;
			polarity = SBU_FLIP;
			count = 0;
		} else {
			count++;
		}
	} else if ((!mux_en) && (sbu2 > USB_HIGH_MV) && (sbu1 < GND_MAX_MV)) {
		/* Check direct connection polarity. */
		if (last != MODE_SBU_CONNECT) {
			last = MODE_SBU_CONNECT;
			polarity = SBU_DIRECT;
			count = 0;
		} else {
			count++;
		}
	/*
	 * If SuzyQ is enabled, we'll poll for a persistent no-signal for
	 * 500ms. Since USB is differential, we should never see GND/GND
	 * while the device is connected.
	 * If disconnected, electrically remove SuzyQ.
	 */
	} else if ((mux_en) && (sbu1 < GND_MAX_MV) && (sbu2 < GND_MAX_MV)) {
		/* Check for SBU disconnect if connected. */
		if (last != MODE_SBU_DISCONNECT) {
			last = MODE_SBU_DISCONNECT;
			count = 0;
		} else {
			count++;
		}
	} else {
		/* Didn't find anything, reset state. */
		last = MODE_SBU_OTHER;
		count = 0;
	}

	/*
	 * We have seen a new state continuously for 500ms.
	 * Let's update the mux to enable/disable SuzyQ appropriately.
	 */
	if (count > 5) {
		if (mux_en) {
			/* Disable mux as it's disconnected now. */
			gpio_set_level(GPIO_SBU_MUX_EN, 0);
			msleep(10);
			CPRINTS("CCD: disconnected.");
		} else {
			/* SBU flip = polarity */
			write_ioexpander(0, 2, polarity);
			gpio_set_level(GPIO_SBU_MUX_EN, 1);
			msleep(10);
			CPRINTS("CCD: connected %s",
				polarity ? "flip" : "noflip");
		}
	}

	/* Measure every 100ms, forever. */
	hook_call_deferred(&ccd_measure_sbu_data, 100 * MSEC);
}

void ccd_enable(int enable)
{
	if (enable) {
		hook_call_deferred(&ccd_measure_sbu_data, 0);
	} else {
		gpio_set_level(GPIO_SBU_MUX_EN, 0);
		hook_call_deferred(&ccd_measure_sbu_data, -1);
	}
}

int board_get_version(void)
{
	static int ver = -1;

	if (ver < 0) {
		uint8_t id0, id1;

		id0 = read_ioexpander_bit(1, 3);
		id1 = read_ioexpander_bit(1, 4);

		ver = (id1 * 2) + id0;
		CPRINTS("Board ID = %d", ver);
	}

	return ver;
}

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

	/*
	 * Write USB3 Mode to PS8742 USB/DP Mux.
	 * 0x0:disable 0x20:enable.
	 */
	i2c_write8(1, 0x10, 0x0, 0x0);

	/* Enable uservo USB by default. */
	init_ioexpander();
	init_uservo_port();
	init_usb3_port();

	/* Clear BBRAM, we don't want any PD state carried over on reset. */
	system_set_bbram(SYSTEM_BBRAM_IDX_PD0, 0);
	system_set_bbram(SYSTEM_BBRAM_IDX_PD1, 0);

	/*
	 * Disable SBU mux. The polarity is set each time a presense is detected
	 * on SBU, and wired thorugh. On missing voltage on SBU. SBU wires are
	 * disconnected.
	 */
	gpio_set_level(GPIO_SBU_MUX_EN, 0);

	/*
	 * Voltage transition needs to occur in lockstep between the CHG and
	 * DUT ports, so initially limit voltage to 5V.
	 */
	pd_set_max_voltage(PD_MIN_MV);

	/* Enable VBUS detection to wake PD tasks fast enough */
	gpio_enable_interrupt(GPIO_USB_DET_PP_CHG);
	gpio_enable_interrupt(GPIO_USB_DET_PP_DUT);

	hook_call_deferred(&ccd_measure_sbu_data, 1000 * MSEC);

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
