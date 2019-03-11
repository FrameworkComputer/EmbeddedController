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
#define GPIOX_I2C_ADDR		0x40
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
	i2c_read8(1, GPIOX_I2C_ADDR, GPIOX_OUT_PORT_A + bank, &tmp);
	if (val)
		tmp |= BIT(gpio);
	else
		tmp &= ~BIT(gpio);
	/* Write back modified output port register */
	i2c_write8(1, GPIOX_I2C_ADDR, GPIOX_OUT_PORT_A + bank, tmp);
}

/* Read a single GPIO input on the tca6416 I2C ioexpander. */
static int read_ioexpander_bit(int bank, int bit)
{
	int tmp;
	int mask = 1 << bit;

	/* Read input port register */
	i2c_read8(1, GPIOX_I2C_ADDR, GPIOX_IN_PORT_A + bank, &tmp);

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
	i2c_write8(1, GPIOX_I2C_ADDR, GPIOX_OUT_PORT_A, 0x0);
	i2c_write8(1, GPIOX_I2C_ADDR, GPIOX_OUT_PORT_B, 0x0);

	/*
	 * Write GPIO direction: strap resistors to input,
	 * all others to output.
	 */
	i2c_write8(1, GPIOX_I2C_ADDR, GPIOX_DIR_PORT_A, 0x0);
	i2c_write8(1, GPIOX_I2C_ADDR, GPIOX_DIR_PORT_B, 0x18);
}

/* Define voltage thresholds for SBU USB detection */
#define GND_MAX_MV	350
#define USB_HIGH_MV	1500

static void ccd_measure_sbu(void);
DECLARE_DEFERRED(ccd_measure_sbu);

static void ccd_measure_sbu(void)
{
	int sbu1;
	int sbu2;

	/* Read sbu voltage levels */
	sbu1 = adc_read_channel(ADC_SBU1_DET);
	sbu2 = adc_read_channel(ADC_SBU2_DET);

	/* USB FS pulls one line high for connect request */
	if ((sbu1 > USB_HIGH_MV) && (sbu2 < GND_MAX_MV)) {
		/* SBU flip = 1 */
		write_ioexpander(0, 2, 1);
		msleep(10);
		CPRINTS("CCD: connected flip");
	} else if ((sbu2 > USB_HIGH_MV) &&
		   (sbu1 < GND_MAX_MV)) {
		/* SBU flip = 0 */
		write_ioexpander(0, 2, 0);
		msleep(10);
		CPRINTS("CCD: connected noflip");
	} else {
		/* Measure again after 100 msec */
		hook_call_deferred(&ccd_measure_sbu_data, 100 * MSEC);
	}
}

static uint8_t ccd_keepalive_enabled;
static int command_keepalive(int argc, char **argv)
{
	int val;

	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	if (argc == 2) {
		if (!parse_bool(argv[1], &val))
			return EC_ERROR_PARAM1;

		ccd_keepalive_enabled = val;
	}
	ccprintf("ccd_keepalive: %sabled\n",
		 ccd_keepalive_enabled ? "en" : "dis");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(keepalive, command_keepalive, "[enable | disable]",
			"Enable CCD keepalive.  Prevents SBU sampling.");

static void check_for_disconnect(void);
DECLARE_DEFERRED(check_for_disconnect);
static void check_for_disconnect(void)
{
	static uint8_t entries;
	int dut_is_connected = pd_is_connected(1);

	entries++;

	if (dut_is_connected) {
		entries = 0;
		return;
	} else if ((entries < 3) && !dut_is_connected) {
		/* Hmm, it's still not connected? Let's keep checking. */
		hook_call_deferred(&check_for_disconnect_data, 100 * MSEC);
		return;
	}

	/*
	 * Hmm, okay.  Maybe the DUT is actually disconnected.  Clear
	 * the CCD keepalive such that the auto flip orientation
	 * detection will work upon a plug in.
	 */
	CPRINTS("DUT seems disconnected.  Clearing CCD keepalive.");
	entries = 0;
	ccd_keepalive_enabled = 0;
}

void ccd_enable(int enable)
{
	if (enable) {
		/*
		 * Unfortunately the polarity detect is designed for real plug
		 * events, and only accurately detects pre-connect idle. If
		 * there's active traffic on the line (like while EC is
		 * rebooting) this could pretty much go either way.  Therefore,
		 * if CCD keepalive is enabled, let's not measure the SBU lines
		 * and leave the mux alone.  Most likely nothing has changed.
		 *
		 * NOTE: Once CCD keepalive has been enabled, it will remained
		 * enabled until the DUT is seen disconnected for at least
		 * 900ms.
		 */
		if (!ccd_keepalive_enabled)
			/* Allow some time following turning on of VBUS */
			hook_call_deferred(&ccd_measure_sbu_data,
					   PD_POWER_SUPPLY_TURN_ON_DELAY);
	} else {
		/* We are not connected to anything */

		/* Disable ccd_measure_sbu deferred call always */
		hook_call_deferred(&ccd_measure_sbu_data, -1);

		/*
		 * In a bit, start checking to see if we're still
		 * disconnected.
		 */
		hook_call_deferred(&check_for_disconnect_data, 600 * MSEC);

		/*
		 * The DUT port has detected a detach event. Don't want to
		 * disconnect the SBU mux here so that the H1 USB console can
		 * remain connected.
		 */
		CPRINTS("CCD: TypeC detach, no change to SBU mux");
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
	i2c_write8(1, 0x20, 0x0, 0x0);

	/* Enable uservo USB by default. */
	init_ioexpander();
	init_uservo_port();
	init_usb3_port();

	/* Clear BBRAM, we don't want any PD state carried over on reset. */
	system_set_bbram(SYSTEM_BBRAM_IDX_PD0, 0);
	system_set_bbram(SYSTEM_BBRAM_IDX_PD1, 0);

	/*
	 * Enable SBU mux. The polarity is set each time a new PD attach event
	 * occurs. But, the SBU mux is not disabled on detach so that the H1 USB
	 * console will survie a DUT EC reset.
	 */
	gpio_set_level(GPIO_SBU_MUX_EN, 1);

	/*
	 * Voltage transition needs to occur in lockstep between the CHG and
	 * DUT ports, so initially limit voltage to 5V.
	 */
	pd_set_max_voltage(PD_MIN_MV);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
