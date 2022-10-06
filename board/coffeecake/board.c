/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Coffeecake dock configuration */

#include "adc.h"
#include "charger/sy21612.h"
#include "clock.h"
#include "common.h"
#include "ec_commands.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "mcdp28x0.h"
#include "registers.h"
#include "task.h"
#include "usb_bb.h"
#include "usb_descriptor.h"
#include "usb_pd.h"
#include "timer.h"
#include "util.h"

static volatile uint64_t hpd_prev_ts;
static volatile int hpd_prev_level;

void hpd_event(enum gpio_signal signal);
void vbus_event(enum gpio_signal signal);
#include "gpio_list.h"

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "charger",
	  .port = I2C_PORT_SY21612,
	  .kbps = 400,
	  .scl = GPIO_I2C0_SCL,
	  .sda = GPIO_I2C0_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/**
 * Hotplug detect deferred task
 *
 * Called after level change on hpd GPIO to evaluate (and debounce) what event
 * has occurred.  There are 3 events that occur on HPD:
 *    1. low  : downstream display sink is deattached
 *    2. high : downstream display sink is attached
 *    3. irq  : downstream display sink signalling an interrupt.
 *
 * The debounce times for these various events are:
 *   HPD_USTREAM_DEBOUNCE_LVL : min pulse width of level value.
 *   HPD_USTREAM_DEBOUNCE_IRQ : min pulse width of IRQ low pulse.
 *
 * lvl(n-2) lvl(n-1)  lvl   prev_delta  now_delta event
 * ----------------------------------------------------
 * 1        0         1     <IRQ        n/a       low glitch (ignore)
 * 1        0         1     >IRQ        <LVL      irq
 * x        0         1     n/a         >LVL      high
 * 0        1         0     <LVL        n/a       high glitch (ignore)
 * x        1         0     n/a         >LVL      low
 */

void hpd_irq_deferred(void)
{
	pd_send_hpd(0, hpd_irq);
}
DECLARE_DEFERRED(hpd_irq_deferred);

void hpd_lvl_deferred(void)
{
	int level = gpio_get_level(GPIO_DP_HPD);

	if (level != hpd_prev_level)
		/* It's a glitch while in deferred or canceled action */
		return;

	pd_send_hpd(0, (level) ? hpd_high : hpd_low);
}
DECLARE_DEFERRED(hpd_lvl_deferred);

void hpd_event(enum gpio_signal signal)
{
	timestamp_t now = get_time();
	int level = gpio_get_level(signal);
	uint64_t cur_delta = now.val - hpd_prev_ts;

	/* store current time */
	hpd_prev_ts = now.val;

	/* All previous hpd level events need to be re-triggered */
	hook_call_deferred(&hpd_lvl_deferred_data, -1);

	/* It's a glitch.  Previous time moves but level is the same. */
	if (cur_delta < HPD_USTREAM_DEBOUNCE_IRQ)
		return;

	if ((!hpd_prev_level && level) &&
	    (cur_delta < HPD_USTREAM_DEBOUNCE_LVL))
		/* It's an irq */
		hook_call_deferred(&hpd_irq_deferred_data, 0);
	else if (cur_delta >= HPD_USTREAM_DEBOUNCE_LVL)
		hook_call_deferred(&hpd_lvl_deferred_data,
				   HPD_USTREAM_DEBOUNCE_LVL);

	hpd_prev_level = level;
}

/* Proto 0 workaround */
void vbus_event(enum gpio_signal signal)
{
	/* Discharge VBUS on DET_L high */
	gpio_set_level(GPIO_PD_DISCHARGE, gpio_get_level(signal));
}

/* USB C VBUS output selection */
void board_set_usb_output_voltage(int mv)
{
	const int ra = 40200;
	const int rb = 10000;
	const int rc = 6650;
	int dac_mv;
	uint32_t dac_val;

	if (mv >= 0) {
		/* vbat = 1.0 * ra/rb + 1.0 - (vdac - 1.0) * ra/rc */
		dac_mv = 1000 + (1000 * rc / rb) + ((1000 - mv) * rc / ra);
		if (dac_mv < 0)
			dac_mv = 0;

		/* Set voltage Vout=Vdac with Vref = 3.3v */
		/* TODO: use Vdda instead */
		dac_val = dac_mv * 4096 / 3300;
		/* Start DAC channel 2 */
		STM32_DAC_DHR12RD = dac_val << 16;
		STM32_DAC_CR = STM32_DAC_CR_EN2;
	} else {
		STM32_DAC_CR = 0;
	}
}

/* Initialize board. */
void board_config_pre_init(void)
{
	/* Enable SYSCFG clock */
	STM32_RCC_APB2ENR |= BIT(0);
	/* Enable DAC interface clock. */
	STM32_RCC_APB1ENR |= BIT(29);
	/* Delay 1 APB clock cycle after the clock is enabled */
	clock_wait_bus_cycles(BUS_APB, 1);
	/* Set 5Vsafe Vdac */
	board_set_usb_output_voltage(5000);
	/* Remap USART DMA to match the USART driver */
	STM32_SYSCFG_CFGR1 |= BIT(9) | BIT(10); /* Remap USART1 RX/TX DMA */
}

#ifdef CONFIG_SPI_FLASH

static void board_init_spi2(void)
{
	/* Remap SPI2 to DMA channels 6 and 7 */
	STM32_SYSCFG_CFGR1 |= BIT(24);

	/* Set pin NSS to general purpose output mode (01b). */
	/* Set pins SCK, MISO, and MOSI to alternate function (10b). */
	STM32_GPIO_MODER(GPIO_B) &= ~0xff000000;
	STM32_GPIO_MODER(GPIO_B) |= 0xa9000000;

	/* Set all four pins to alternate function 0 */
	STM32_GPIO_AFRH(GPIO_B) &= ~(0xffff0000);

	/* Set all four pins to output push-pull */
	STM32_GPIO_OTYPER(GPIO_B) &= ~(0xf000);

	/* Set pullup on NSS */
	STM32_GPIO_PUPDR(GPIO_B) |= 0x1000000;

	/* Set all four pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= BIT(14);
	STM32_RCC_APB1RSTR &= ~BIT(14);

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;
}
#endif /* CONFIG_SPI_FLASH */

static void factory_validation_deferred(void)
{
	struct mcdp_info info;

	mcdp_enable();

	/* test mcdp via serial to validate function */
	if (!mcdp_get_info(&info) && (MCDP_FAMILY(info.family) == 0x0010) &&
	    (MCDP_CHIPID(info.chipid) == 0x2850)) {
		pd_log_event(PD_EVENT_VIDEO_CODEC,
			     PD_LOG_PORT_SIZE(0, sizeof(info)), 0, &info);
	}

	mcdp_disable();
}
DECLARE_DEFERRED(factory_validation_deferred);

static void board_post_init(void)
{
	sy21612_enable_regulator(1);
	/*
	 * AC powered  - DRP SOURCE
	 * DUT powered - DRP SINK
	 */
	pd_set_dual_role(0, gpio_get_level(GPIO_AC_PRESENT_L) ?
				    PD_DRP_FORCE_SINK :
				    PD_DRP_FORCE_SOURCE);
}
DECLARE_DEFERRED(board_post_init);

/* Initialize board. */
static void board_init(void)
{
	timestamp_t now;
#ifdef CONFIG_SPI_FLASH
	board_init_spi2();
#endif
	now = get_time();
	hpd_prev_level = gpio_get_level(GPIO_DP_HPD);
	hpd_prev_ts = now.val;
	gpio_enable_interrupt(GPIO_DP_HPD);
	gpio_enable_interrupt(GPIO_CHARGER_INT);
	gpio_enable_interrupt(GPIO_USB_C_VBUS_DET_L);
	/* Set PD_DISCHARGE initial state */
	gpio_set_level(GPIO_PD_DISCHARGE,
		       gpio_get_level(GPIO_USB_C_VBUS_DET_L));

	/* Delay needed to allow HDMI MCU to boot. */
	hook_call_deferred(&factory_validation_deferred_data, 200 * MSEC);
	/* Initialize buck-boost converter */
	hook_call_deferred(&board_post_init_data, 0);
}

DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_CH_CC1_PD] = { "USB_C_CC1_PD", 3300, 4096, 0, STM32_AIN(1) },
	[ADC_VBUS_MON] = { "VBUS_MON", 13200, 4096, 0, STM32_AIN(2) },
	[ADC_DAC_REF_TP28] = { "DAC_REF_TP28", 3300, 4096, 0, STM32_AIN(4) },
	[ADC_DAC_VOLT] = { "DAC_VOLT", 3300, 4096, 0, STM32_AIN(5) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Hoho"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_BB_URL] = USB_STRING_DESC(USB_GOOGLE_TYPEC_URL),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/**
 * USB configuration
 * Any type-C device with alternate mode capabilities must have the following
 * set of descriptors.
 *
 * 1. Standard Device
 * 2. BOS
 *    2a. Container ID
 *    2b. Billboard Caps
 */
struct my_bos {
	struct usb_bos_hdr_descriptor bos;
	struct usb_contid_caps_descriptor contid_caps;
	struct usb_bb_caps_base_descriptor bb_caps;
	struct usb_bb_caps_svid_descriptor bb_caps_svids[1];
};

static struct my_bos bos_desc = {
	.bos = {
		.bLength = USB_DT_BOS_SIZE,
		.bDescriptorType = USB_DT_BOS,
		.wTotalLength = (USB_DT_BOS_SIZE + USB_DT_CONTID_SIZE +
				 USB_BB_CAPS_BASE_SIZE +
				 USB_BB_CAPS_SVID_SIZE * 1),
		.bNumDeviceCaps = 2,  /* contid + bb_caps */
	},
	.contid_caps =	{
		.bLength = USB_DT_CONTID_SIZE,
		.bDescriptorType = USB_DT_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_DC_DTYPE_CONTID,
		.bReserved = 0,
		.ContainerID = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	},
	.bb_caps = {
		.bLength = (USB_BB_CAPS_BASE_SIZE + USB_BB_CAPS_SVID_SIZE * 1),
		.bDescriptorType = USB_DT_DEVICE_CAPABILITY,
		.bDevCapabilityType = USB_DC_DTYPE_BILLBOARD,
		.iAdditionalInfoURL = USB_STR_BB_URL,
		.bNumberOfAlternateModes = 1,
		.bPreferredAlternateMode = 1,
		.VconnPower = 0,
		.bmConfigured = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		.bReserved = 0,
	},
	.bb_caps_svids = {
		{
			.wSVID = 0xff01, /* TODO(tbroch) def'd in other CL remove hardcode */
			.bAlternateMode = 1,
			.iAlternateModeString = USB_STR_BB_URL, /* TODO(crosbug.com/p/32687) */
		},
	},
};

const struct bos_context bos_ctx = {
	.descp = (void *)&bos_desc,
	.size = sizeof(struct my_bos),
};
