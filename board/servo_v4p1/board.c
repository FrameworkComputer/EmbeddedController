/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo V4p1 configuration */

#include "adc.h"
#include "chg_control.h"
#include "common.h"
#include "console.h"
#include "dacs.h"
#include "driver/ioexpander/tca64xxa.h"
#include "ec_version.h"
#include "fusb302b.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ina231s.h"
#include "ioexpanders.h"
#include "pathsel.h"
#include "pi3usb9201.h"
#include "poweron_conf.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "tusb1064.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "usb-stream.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_spi.h"
#include "util.h"

#include <driver/gl3590.h>

/*
 * Important part of servo functionality is dependent on poweron config feature.
 * We enable all USB ports and CCD in apply_poweron_config function. Therefore
 * make sure nobody would try to compile servo without CONFIG_POWERON_CONF.
 * Also note because of this ifdef there is no additional need for ifdef around
 * apply_poweron_config()/board_read|write_poweron_conf calls.
 */
#if !defined(CONFIG_POWERON_CONF) || !defined(CONFIG_POWERON_CONF_LEN)
#error "Servo needs POWERON_CONF feature to ensure basic functionalities."
#endif

#ifdef SECTION_IS_RO
#define CROS_EC_SECTION "RO"
#else
#define CROS_EC_SECTION "RW"
#endif

/******************************************************************************
 * GPIO interrupt handlers.
 */
#ifdef SECTION_IS_RO
static void vbus0_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C0);
}

static void vbus1_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C1);
}

static void tca_evt(enum gpio_signal signal)
{
	irq_ioexpanders();
}

/*
 * Some DUTs are known to be incompatible with servo_v4p1 and USB3.
 * Due to this USB3 to DUT is forced to be disabled by default.
 * This command can enable or disable USB3 to DUT manually.
 * This command is issued during initialization of servod,
 * automatically enabling USB3 only on DUTs that are known to work
 * with USB3 servo.
 * For more information please refer to b/254857085 and b/263573379.
 */
static bool usb3_to_dut_enable;
static int cmd_dut_usb3(int argc, const char *argv[])
{
	mux_state_t mux_state;

	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "enabled") || !strcasecmp(argv[1], "enable")) {
		/*
		 * Need to set this flag before usb_mux_set to prevent
		 * calling additional set in board_tusb1064_set.
		 */
		usb3_to_dut_enable = true;

		/* Need to reset DUT hub and force re-enumeration. */
		gpio_set_level(GPIO_DUT_HUB_USB_RESET_L, 0);

		/* Overwrite current Type-C mux state to enable USB3. */
		mux_state = usb_mux_get(DUT) | USB_PD_MUX_USB_ENABLED;

		usb_mux_set(DUT, mux_state, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(DUT)));

		/* Delay enabling DUT hub to avoid enumeration problems. */
		crec_usleep(MSEC);
		gpio_set_level(GPIO_DUT_HUB_USB_RESET_L, 1);
	} else if (!strcasecmp(argv[1], "disabled") ||
		   !strcasecmp(argv[1], "disable")) {
		/*
		 * Make sure this flag is set to avoid calling additional
		 * mux set operation in board specific routine.
		 */
		usb3_to_dut_enable = true;

		/* No need to reset hub, devices should auto re-enumerate. */
		mux_state = usb_mux_get(DUT) & ~USB_PD_MUX_USB_ENABLED;

		usb_mux_set(DUT, mux_state, USB_SWITCH_CONNECT,
			    polarity_rm_dts(pd_get_polarity(DUT)));
		usb3_to_dut_enable = false;
	} else if (argc != 1) {
		ccprintf("Invalid argument: %s\n", argv[1]);
		return EC_ERROR_INVAL;
	}

	ccprintf("USB3 to DUT: %s\n",
		 usb3_to_dut_enable ? "enabled" : "disabled");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dut_usb3, cmd_dut_usb3, "dut_usb3 [enabled/disabled]>",
			"Enable or disable USB3 to DUT. Note that after every "
			"'dut_usb3 enabled' USB3 is enabled once and than only "
			"allowed, not forced. Some other part of servo logic "
			"(e.g. pd stack) can still enable/disable it.");

/*
 * TUSB1064 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration.
 * Also adds USB3 quirk.
 */
static int board_tusb1064_set(const struct usb_mux *me, mux_state_t mux_state)
{
	int rv = EC_SUCCESS;
	bool unused;

	/*
	 * Apply 10dB gain. Note, this value is selected to match the gain that
	 * would be set by default if the 2 GPIO gain set pins are left
	 * floating.
	 */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		rv = tusb1064_set_dp_rx_eq(me, TUSB1064_DP_EQ_RX_10_0_DB);
		/*
		 * There is no need to perform any of additional
		 * USB3 workaround related logic if we are using DP, so return.
		 */
		return rv;
	}

	/*
	 * This function is issued after standard set operation.
	 * Logic below overwrites any mux set operation issued by e.g.
	 * pd stack. It prevents using USB3 to DUT on servo, unless
	 * it is explicitly allowed.
	 * If user is sure that specific DUT works with USB3 servo_v4p1,
	 * can skip this logic setting usb3_to_dut_enable flag.
	 * It can be done via console command usb3_dut [on\off].
	 */
	if (!usb3_to_dut_enable) {
		/*
		 * In this point servo is already connected to DUT.
		 * USB3 can be already enabled for short moment.
		 * Keep DUT hub in reset until MUX is finally set
		 * (USB3 disabled) to prevent any enumeration issues.
		 */
		gpio_set_level(GPIO_DUT_HUB_USB_RESET_L, 0);

		/*
		 * Overwrite any set operation to disable USB3.
		 * Note that we can use internal driver call as mux driver
		 * already locked mutex inside usb_mux_set operation.
		 * Also note that we can not use usb_mux_set to prevent
		 * infinite recursion.
		 */
		rv = tusb1064_set_mux(me, mux_state & ~USB_PD_MUX_USB_ENABLED,
				      &unused);

		/* MUX is set, add preventive delay and enable DUT USB hub. */
		crec_usleep(MSEC);
		gpio_set_level(GPIO_DUT_HUB_USB_RESET_L, 1);
	}
	return rv;
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[CHG] = {
		/* CHG port connected directly to USB 3.0 hub, no mux */
	},
	[DUT] = {
		/* DUT port with UFP mux */
		.mux =
			&(const struct usb_mux){
				.usb_port = DUT,
				.i2c_port = I2C_PORT_MASTER,
				.i2c_addr_flags = TUSB1064_I2C_ADDR10_FLAGS,
				.driver = &tusb1064_usb_mux_driver,
				.board_set = &board_tusb1064_set,
			},
	}
};

static volatile uint64_t hpd_prev_ts;
static volatile int hpd_prev_level;

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
	int dp_mode = pd_alt_mode(1, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	if (dp_mode) {
		pd_send_hpd(DUT, hpd_irq);
		ccprintf("HPD IRQ");
	}
}
DECLARE_DEFERRED(hpd_irq_deferred);

void hpd_lvl_deferred(void)
{
	int level = gpio_get_level(GPIO_DP_HPD);
	int dp_mode = pd_alt_mode(1, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	if (level != hpd_prev_level) {
		/* It's a glitch while in deferred or canceled action */
		return;
	}

	if (dp_mode) {
		pd_send_hpd(DUT, level ? hpd_high : hpd_low);
		ccprintf("HPD: %d", level);
	}
}
DECLARE_DEFERRED(hpd_lvl_deferred);

static void dp_evt(enum gpio_signal signal)
{
	timestamp_t now = get_time();
	int level = gpio_get_level(signal);
	uint64_t cur_delta = now.val - hpd_prev_ts;

	/* Store current time */
	hpd_prev_ts = now.val;

	/* All previous hpd level events need to be re-triggered */
	hook_call_deferred(&hpd_lvl_deferred_data, -1);

	/* It's a glitch.  Previous time moves but level is the same. */
	if (cur_delta < HPD_USTREAM_DEBOUNCE_IRQ)
		return;

	if ((!hpd_prev_level && level) &&
	    (cur_delta < HPD_USTREAM_DEBOUNCE_LVL)) {
		/* It's an irq */
		hook_call_deferred(&hpd_irq_deferred_data, 0);
	} else if (cur_delta >= HPD_USTREAM_DEBOUNCE_LVL) {
		hook_call_deferred(&hpd_lvl_deferred_data,
				   HPD_USTREAM_DEBOUNCE_LVL);
	}

	hpd_prev_level = level;
}

static void tcpc_evt(enum gpio_signal signal)
{
	update_status_fusb302b();
}

#define HOST_HUB 0
struct uhub_i2c_iface_t uhub_config[] = {
	{ I2C_PORT_MASTER, GL3590_I2C_ADDR0 },
};

static void host_hub_evt(void)
{
	gl3590_irq_handler(HOST_HUB);
}
DECLARE_DEFERRED(host_hub_evt);

static void hub_evt(enum gpio_signal signal)
{
	hook_call_deferred(&host_hub_evt_data, 0);
}

static void dut_pwr_evt(enum gpio_signal signal)
{
	ccprintf("dut_pwr_evt\n");
}

void ext_hpd_detection_enable(int enable)
{
	if (enable) {
		timestamp_t now = get_time();

		hpd_prev_level = gpio_get_level(GPIO_DP_HPD);
		hpd_prev_ts = now.val;
		gpio_enable_interrupt(GPIO_DP_HPD);
	} else {
		gpio_disable_interrupt(GPIO_DP_HPD);
	}
}
#endif /* SECTION_IS_RO */

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

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
	[ADC_CHG_CC1_PD] = { "CHG_CC1_PD", 3300, 4096, 0, STM32_AIN(2) },
	[ADC_CHG_CC2_PD] = { "CHG_CC2_PD", 3300, 4096, 0, STM32_AIN(4) },
	[ADC_DUT_CC1_PD] = { "DUT_CC1_PD", 3300, 4096, 0, STM32_AIN(0) },
	[ADC_DUT_CC2_PD] = { "DUT_CC2_PD", 3300, 4096, 0, STM32_AIN(5) },
	[ADC_SBU1_DET] = { "SBU1_DET", 3300, 4096, 0, STM32_AIN(3) },
	[ADC_SBU2_DET] = { "SBU2_DET", 3300, 4096, 0, STM32_AIN(7) },
	[ADC_SUB_C_REF] = { "SUB_C_REF", 3300, 4096, 0, STM32_AIN(1) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE 16
#define USB_STREAM_TX_SIZE 16

/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 =
	QUEUE_DIRECT(64, uint8_t, usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart3_to_usb, usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb, USB_IFACE_USART3_STREAM,
		  USB_STR_USART3_STREAM_NAME, USB_EP_USART3_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart3,
		  usart3_to_usb)

/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 =
	QUEUE_DIRECT(64, uint8_t, usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw, usart_rx_interrupt, usart_tx_interrupt, 9600, 0,
		     usart4_to_usb, usb_to_usart4);

USB_STREAM_CONFIG_USART_IFACE(usart4_usb, USB_IFACE_USART4_STREAM,
			      USB_STR_USART4_STREAM_NAME, USB_EP_USART4_STREAM,
			      USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE,
			      usb_to_usart4, usart4_to_usb, usart4)

/*
 * Define usb interface descriptor for the `EMPTY` usb interface, to satisfy
 * UEFI and kernel requirements (see b/183857501).
 */
const struct usb_interface_descriptor USB_IFACE_DESC(USB_IFACE_EMPTY) = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = USB_IFACE_EMPTY,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,
};

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Servo V4p1"),
	[USB_STR_SERIALNO] = USB_STRING_DESC("1234-a"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo EC Shell"),
	[USB_STR_USART3_STREAM_NAME] = USB_STRING_DESC("DUT UART"),
	[USB_STR_USART4_STREAM_NAME] = USB_STRING_DESC("Atmega UART"),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

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
 * Initialize board.
 */

int board_get_version(void)
{
	return board_id_det();
}

#ifdef SECTION_IS_RO
/* Forward declaration */
static void evaluate_input_power_def(void);
DECLARE_DEFERRED(evaluate_input_power_def);

static void evaluate_input_power_def(void)
{
	int state;
	static int retry = 3;

	/* Wait until host hub INTR# signal is asserted */
	state = gpio_get_level(GPIO_USBH_I2C_BUSY_INT);
	if ((state == 0) && retry--) {
		hook_call_deferred(&evaluate_input_power_def_data, 100 * MSEC);
		return;
	}

	if (retry == 0)
		CPRINTF("Host hub I2C isn't online, expect issues with its "
			"behaviour\n");

	gpio_enable_interrupt(GPIO_USBH_I2C_BUSY_INT);

	gl3590_init(HOST_HUB);

	apply_poweron_conf();
}
#endif

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
	crec_usleep(MSEC);

	init_pi3usb9201();

	/* Clear BBRAM, we don't want any PD state carried over on reset. */
	system_set_bbram(SYSTEM_BBRAM_IDX_PD0, 0);
	system_set_bbram(SYSTEM_BBRAM_IDX_PD1, 0);

#ifdef SECTION_IS_RO
	init_ioexpanders();
	CPRINTS("Board ID is %d", board_id_det());

	init_dacs();
	apply_poweron_conf();
	init_ina231s();
	init_fusb302b(1);
	vbus_dischrg_en(0);

	/* Bring atmel part out of reset */
	atmel_reset_l(1);

	/*
	 * Get data about available input power. Defer this check, since we need
	 * to wait for USB2/USB3 enumeration on host hub as well as I2C
	 * interface of this hub needs to be initialized. Genesys recommends at
	 * least 100ms.
	 */
	hook_call_deferred(&evaluate_input_power_def_data, 100 * MSEC);

	/* Enable VBUS detection to wake PD tasks fast enough */
	gpio_enable_interrupt(GPIO_USB_DET_PP_CHG);
	gpio_enable_interrupt(GPIO_USB_DET_PP_DUT);

	gpio_enable_interrupt(GPIO_STM_FAULT_IRQ_L);
	gpio_enable_interrupt(GPIO_DP_HPD);
	gpio_enable_interrupt(GPIO_DUT_PWR_IRQ_ODL);

	/* Disable power to DUT by default */
	chg_power_select(CHG_POWER_OFF);

	/*
	 * Voltage transition needs to occur in lockstep between the CHG and
	 * DUT ports, so initially limit voltage to 5V.
	 */
	pd_set_max_voltage(PD_MIN_MV);

#else /* SECTION_IS_RO */
	CPRINTS("Board ID is %d", board_id_det());
#endif /* SECTION_IS_RO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#ifdef SECTION_IS_RO
void tick_event(void)
{
	static int i = 0;

	i++;
	switch (i) {
	case 1:
		tca_gpio_dbg_led_k_odl(1);
		break;
	case 2:
		break;
	case 3:
		tca_gpio_dbg_led_k_odl(0);
		break;
	case 4:
		i = 0;
		break;
	}
}
DECLARE_HOOK(HOOK_TICK, tick_event, HOOK_PRIO_DEFAULT);

struct ioexpander_config_t ioex_config[] = {
	[0] = { .drv = &tca64xxa_ioexpander_drv,
		.i2c_host_port = TCA6416A_PORT,
		.i2c_addr_flags = TCA6416A_ADDR,
		.flags = IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6416A },
	[1] = { .drv = &tca64xxa_ioexpander_drv,
		.i2c_host_port = TCA6424A_PORT,
		.i2c_addr_flags = TCA6424A_ADDR,
		.flags = IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6424A }
};

#endif /* SECTION_IS_RO */
