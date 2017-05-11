/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <endian.h>

#include "clock.h"
#include "common.h"
#include "console.h"
#include "dcrypto/dcrypto.h"
#include "device_state.h"
#include "ec_version.h"
#include "extension.h"
#include "flash.h"
#include "flash_config.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "i2cs.h"
#include "init_chip.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "rdd.h"
#include "registers.h"
#include "scratch_reg1.h"
#include "signed_header.h"
#include "spi.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "tpm_registers.h"
#include "trng.h"
#include "uart_bitbang.h"
#include "uartn.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_spi.h"
#include "usb_i2c.h"
#include "util.h"
#include "wp.h"

/* Define interrupt and gpio structs */
#include "gpio_list.h"

#include "cryptoc/sha.h"

/*
 * Need to include Implementation.h here to make sure that NVRAM size
 * definitions match across different git repos.
 *
 * MAX() definition from include/utils.h does not work in Implementation.h, as
 * it is used in a preprocessor expression there;
 *
 * SHA_DIGEST_SIZE is defined to be the same in both git repos, but using
 * different expressions.
 *
 * To untangle compiler errors let's just undefine MAX() and SHA_DIGEST_SIZE
 * here, as nether is necessary in this case: all we want from
 * Implementation.h at this point is the definition for NV_MEMORY_SIZE.
 */
#undef MAX
#undef SHA_DIGEST_SIZE
#include "Implementation.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

#define NVMEM_TPM_SIZE ((sizeof((struct nvmem_partition *)0)->buffer) \
			- NVMEM_CR50_SIZE)

/*
 * Make sure NV memory size definition in Implementation.h matches reality. It
 * should be set to
 *
 * NVMEM_PARTITION_SIZE - NVMEM_CR50_SIZE - 8
 *
 * Both of these macros are defined in board.h.
 */
BUILD_ASSERT(NVMEM_TPM_SIZE == NV_MEMORY_SIZE);

/* NvMem user buffer lengths table */
uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
	NVMEM_TPM_SIZE,
	NVMEM_CR50_SIZE
};

static int device_state_changed(enum device_type device,
				enum device_state state);

/*  Board specific configuration settings */
static uint32_t board_properties;
static uint8_t reboot_request_posted;

/* Which UARTs we'd like to be able to bitbang. */
struct uart_bitbang_properties bitbang_config = {
	.uart = UART_EC,
	.tx_gpio = GPIO_DETECT_SERVO, /* This is TX to EC console. */
	.rx_gpio = GPIO_EC_TX_CR50_RX,
	/*
	 * The rx/tx_pinmux_regval values MUST agree with the pin config for
	 * both the TX and RX GPIOs in gpio.inc.  Don't change one without
	 * changing the other.
	 */
	.tx_pinmux_reg = GBASE(PINMUX) + GOFFSET(PINMUX, DIOB5_SEL),
	.tx_pinmux_regval = GC_PINMUX_GPIO1_GPIO3_SEL,
	.rx_pinmux_reg = GBASE(PINMUX) + GOFFSET(PINMUX, DIOB6_SEL),
	.rx_pinmux_regval = GC_PINMUX_GPIO1_GPIO4_SEL,
};

extern struct deferred_data ec_uart_deferred__data;
void ec_tx_cr50_rx(enum gpio_signal signal)
{
	uart_bitbang_receive_char(UART_EC);
	/* Let the USART module know that there's new bits to consume. */
	hook_call_deferred(&ec_uart_deferred__data, 0);
}

int board_has_ap_usb(void)
{
	return !!(board_properties & BOARD_USB_AP);
}

int board_use_plt_rst(void)
{
	return !!(board_properties & BOARD_USE_PLT_RESET);
}

int board_rst_pullup_needed(void)
{
	return !!(board_properties & BOARD_NEEDS_SYS_RST_PULL_UP);
}

int board_tpm_uses_i2c(void)
{
	return !!(board_properties & BOARD_SLAVE_CONFIG_I2C);
}

int board_tpm_uses_spi(void)
{
	return !!(board_properties & BOARD_SLAVE_CONFIG_SPI);
}

/* Get header address of the backup RW copy. */
const struct SignedHeader *get_other_rw_addr(void)
{
	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
		return (const struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW_B);

	return (const struct SignedHeader *)
		get_program_memory_addr(SYSTEM_IMAGE_RW);
}

/* Return true if the other RW is not ready to run. */
static int other_rw_is_inactive(void)
{
	const struct SignedHeader *header = get_other_rw_addr();

	return !!(header->image_size & TOP_IMAGE_SIZE_BIT);
}

/* I2C Port definition */
const struct i2c_port_t i2c_ports[]  = {
	{"master", I2C_PORT_MASTER, 100,
	 GPIO_I2C_SCL_INA, GPIO_I2C_SDA_INA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Strapping pin info structure */
#define STRAP_PIN_DELAY_USEC  100
enum strap_list {
	a0,
	a1,
	b0,
	b1,
};

struct strap_desc {
	/* GPIO enum from gpio.inc for the strap pin */
	uint8_t gpio_signal;
	/* Offset into pinmux register section for pad SEL register */
	uint8_t sel_offset;
	/* Entry in the pinmux peripheral selector table for pad */
	uint8_t pad_select;
	const char *pad_name;
};

struct board_cfg {
	/* Value the strap pins should read for a given board */
	uint8_t strap_cfg;
	/* Properties required for a given board */
	uint32_t board_properties;
};

/*
 * This table contains both the GPIO and pad specific information required to
 * configure each strapping pin to be either a GPIO input or output.
 */
const struct strap_desc strap_regs[] = {
	{GPIO_STRAP_A0, GOFFSET(PINMUX, DIOA1_SEL), GC_PINMUX_DIOA1_SEL, "a1"},
	{GPIO_STRAP_A1, GOFFSET(PINMUX, DIOA9_SEL), GC_PINMUX_DIOA9_SEL, "a9"},
	{GPIO_STRAP_B0, GOFFSET(PINMUX, DIOA6_SEL), GC_PINMUX_DIOA6_SEL, "a6"},
	{GPIO_STRAP_B1, GOFFSET(PINMUX, DIOA12_SEL), GC_PINMUX_DIOA12_SEL,
	 "a12"},
};

#define BOARD_PROPERTIES_DEFAULT (BOARD_SLAVE_CONFIG_I2C | BOARD_USE_PLT_RESET \
				  | BOARD_USB_AP)
static struct board_cfg board_cfg_table[] = {
	/* SPI Variants: DIOA12 = 1M PD, DIOA6 = 1M PD */
	/* Kevin/Gru: DI0A9 = 5k PD, DIOA1 = 1M PU */
	{ 0x02, BOARD_SLAVE_CONFIG_SPI | BOARD_NEEDS_SYS_RST_PULL_UP },
	/* Poppy: DI0A9 = 1M PU, DIOA1 = 1M PU */
	{ 0x0A, BOARD_SLAVE_CONFIG_SPI | BOARD_USB_AP | BOARD_USE_PLT_RESET },

	/* I2C Variants: DIOA9 = 1M PD, DIOA1 = 1M PD */
	/* Reef/Eve: DIOA12 = 5k PD, DIOA6 = 1M PU */
	{ 0x20, BOARD_SLAVE_CONFIG_I2C | BOARD_USB_AP | BOARD_USE_PLT_RESET },
	/* Rowan: DIOA12 = 5k PD, DIOA6 = 5k PU */
	{ 0x30, BOARD_SLAVE_CONFIG_I2C },
};

void post_reboot_request(void)
{
	/* Reboot the device next time TPM reset is requested. */
	reboot_request_posted = 1;
}

/*****************************************************************************/
/*                                                                           */

/*
 * Battery cutoff monitor is needed on the devices where hardware alone does
 * not provide proper battery cutoff functionality.
 *
 * The sequence is as follows: set up an interrupt to react to the charger
 * disconnect event. When the interrupt happens observe status of the buttons
 * connected to PWRB_IN and KEY0_IN.
 *
 * If both are pressed, start the 5 second timeout, while keeping monitoring
 * the charger connection state. If it remains disconnected for the entire
 * duration - generate 5 second pulses on EC_RST_L and BAT_EN outputs.
 *
 * In reality the BAT_EN output pulse will cause the complete power cut off,
 * so strictly speaking the code does not need to do anything once BAT_EN
 * output is deasserted.
 */

/* Time to wait before initiating battery cutoff procedure. */
#define CUTOFF_TIMEOUT_US (5 * SECOND)

/* A timeout hook to run in the end of the 5 s interval. */
static void ac_stayed_disconnected(void)
{
	uint32_t saved_override_state;

	CPRINTS("%s", __func__);

	/* assert EC_RST_L and deassert BAT_EN */
	GREG32(RBOX, ASSERT_EC_RST) = 1;

	/*
	 * BAT_EN needs to use the RBOX override ability, bit 1 is battery
	 * disable bit.
	 */
	saved_override_state = GREG32(RBOX, OVERRIDE_OUTPUT);
	GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, VAL, 0); /* Setting it to zero. */
	GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, OEN, 1);
	GWRITE_FIELD(RBOX, OVERRIDE_OUTPUT, EN, 1);


	msleep(5000);

	/*
	 * The system was supposed to be shut down the moment battery
	 * disconnect was asserted, but if we made it here we might as well
	 * restore the original state.
	 */
	GREG32(RBOX, OVERRIDE_OUTPUT) = saved_override_state;
	GREG32(RBOX, ASSERT_EC_RST) = 0;
}
DECLARE_DEFERRED(ac_stayed_disconnected);

/*
 * Just a shortcut to make use of these AC power interrupt states better
 * readable. RED means rising edge and FED means falling edge.
 */
enum {
	ac_pres_red = GC_RBOX_INT_STATE_INTR_AC_PRESENT_RED_MASK,
	ac_pres_fed = GC_RBOX_INT_STATE_INTR_AC_PRESENT_FED_MASK,
	buttons_not_pressed = GC_RBOX_CHECK_INPUT_KEY0_IN_MASK |
		GC_RBOX_CHECK_INPUT_PWRB_IN_MASK
};

/*
 * ISR reacting to both falling and raising edges of the AC_PRESENT signal.
 * Falling edge indicates pulling out of the charger cable and vice versa.
 */
static void ac_power_state_changed(void)
{
	uint32_t req;

	/* Get current status and clear it. */
	req = GREG32(RBOX, INT_STATE) & (ac_pres_red | ac_pres_fed);
	GREG32(RBOX, INT_STATE) = req;

	CPRINTS("%s: status 0x%x", __func__, req);

	/* Raising edge gets priority, stop timeout timer and go. */
	if (req & ac_pres_red) {
		hook_call_deferred(&ac_stayed_disconnected_data, -1);
		return;
	}

	/*
	 * If this is not a falling edge, or either of the buttons is not
	 * pressed - bail out.
	 */
	if (!(req & ac_pres_fed) ||
	    (GREG32(RBOX, CHECK_INPUT) & buttons_not_pressed))
		return;

	/*
	 * Charger cable was yanked while the power and key0 buttons were kept
	 * pressed - user wants a battery cut off.
	 */
	hook_call_deferred(&ac_stayed_disconnected_data, CUTOFF_TIMEOUT_US);
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_AC_PRESENT_RED_INT, ac_power_state_changed, 1);
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_AC_PRESENT_FED_INT, ac_power_state_changed, 1);

/* Enable interrupts on plugging in and yanking out of the charger cable. */
static void set_up_battery_cutoff_monitor(void)
{
	/* It is set in idle.c also. */
	GWRITE_FIELD(RBOX, WAKEUP, ENABLE, 1);

	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_AC_PRESENT_RED, 1);
	GWRITE_FIELD(RBOX, INT_ENABLE, INTR_AC_PRESENT_FED, 1);

	task_enable_irq(GC_IRQNUM_RBOX0_INTR_AC_PRESENT_RED_INT);
	task_enable_irq(GC_IRQNUM_RBOX0_INTR_AC_PRESENT_FED_INT);
}
/*                                                                           */
/*****************************************************************************/

/*
 * There's no way to trigger on both rising and falling edges, so force a
 * compiler error if we try. The workaround is to use the pinmux to connect
 * two GPIOs to the same input and configure each one for a separate edge.
 */
#define GPIO_INT(name, pin, flags, signal)	\
	BUILD_ASSERT(((flags) & GPIO_INT_BOTH) != GPIO_INT_BOTH);
#include "gpio.wrap"

static void init_pmu(void)
{
	clock_enable_module(MODULE_PMU, 1);

	/* This boot sequence may be a result of previous soft reset,
	 * in which case the PMU low power sequence register needs to
	 * be reset. */
	GREG32(PMU, LOW_POWER_DIS) = 0;

	/* Enable wakeup interrupt */
	task_enable_irq(GC_IRQNUM_PMU_INTR_WAKEUP_INT);
	GWRITE_FIELD(PMU, INT_ENABLE, INTR_WAKEUP, 1);
}

void pmu_wakeup_interrupt(void)
{
	int exiten, wakeup_src;
	static int count;

	delay_sleep_by(1 * MSEC);

	wakeup_src = GR_PMU_EXITPD_SRC;

	/* Clear interrupt state */
	GWRITE_FIELD(PMU, INT_STATE, INTR_WAKEUP, 1);

	/* Clear pmu reset */
	GWRITE(PMU, CLRRST, 1);

	/*
	 * This will print '.' every time cr50 resumes from regular sleep.
	 * During sleep Cr50 wakes up every half second for HOOK_TICK, so that
	 * is around the rate cr50 will print '.' while it is idle.
	 */
	ccprintf(".");
	if (!(count % 50))
		ccprintf("\n");
	count++;

	if (wakeup_src & GC_PMU_EXITPD_SRC_PIN_PD_EXIT_MASK) {
		/*
		 * If any wake pins are edge triggered, the pad logic latches
		 * the wakeup. Clear EXITEN0 to reset the wakeup logic.
		 */
		exiten = GREG32(PINMUX, EXITEN0);
		GREG32(PINMUX, EXITEN0) = 0;
		GREG32(PINMUX, EXITEN0) = exiten;

		/*
		 * Delay sleep long enough for a SPI slave transaction to start
		 * or for the system to be reset.
		 */
		delay_sleep_by(5 * SECOND);
	}

	/* Trigger timer0 interrupt */
	if (wakeup_src & GC_PMU_EXITPD_SRC_TIMELS0_PD_EXIT_TIMER0_MASK)
		task_trigger_irq(GC_IRQNUM_TIMELS0_TIMINT0);

	/* Trigger timer1 interrupt */
	if (wakeup_src & GC_PMU_EXITPD_SRC_TIMELS0_PD_EXIT_TIMER1_MASK)
		task_trigger_irq(GC_IRQNUM_TIMELS0_TIMINT1);
}
DECLARE_IRQ(GC_IRQNUM_PMU_INTR_WAKEUP_INT, pmu_wakeup_interrupt, 1);

void board_configure_deep_sleep_wakepins(void)
{
	/*
	 * Disable the i2c and spi slave wake sources since the TPM is
	 * not being used and reenable them in their init functions on
	 * resume.
	 */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA12, 0); /* SPS_CS_L */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA1, 0);  /* I2CS_SDA */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA9, 0);  /* I2CS_SCL */

	/* Remove the pulldown on EC uart tx and disable the input */
	GWRITE_FIELD(PINMUX, DIOB5_CTL, PD, 0);
	GWRITE_FIELD(PINMUX, DIOB5_CTL, IE, 0);

	/*
	 * Whether it is a short pulse or long one waking on the high level is
	 * fine because the goal of the system reset signal is to reset the
	 * TPM and after resuming from deep sleep the TPM will be reset. Cr50
	 * doesn't need to read the low value and then reset.
	 */
	if (board_use_plt_rst()) {
		/*
		 * If the board includes plt_rst_l, configure Cr50 to resume on
		 * the rising edge of this signal.
		 */
		/* Disable plt_rst_l as a wake pin */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 0);
		/*
		 * Reconfigure it to be level sensitive so that we are
		 * guaranteed to wake up if the level turns up, no need to
		 * worry about missing the rising edge.
		 */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM3, 0);
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM3, 0);  /* wake on high */
		/* enable powerdown exit */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 1);
	} else {
		/*
		 * DIOA3 is GPIO_DETECT_AP which is used to detect if the AP
		 * is in S0. If the AP is in s0, cr50 should not be in deep
		 * sleep so wake up.
		 */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOA3, 0); /* level sensitive */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOA3, 0);  /* wake on high */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOA3, 1);

		 /*
		  * Configure cr50 to wake when sys_rst_l is asserted. It is
		  * wake on low to make sure that Cr50 is awake to detect the
		  * rising edge of sys_rst_l. This will keep Cr50 awake the
		  * entire time sys_rst_l is asserted.
		  */
		/* Disable sys_rst_l as a wake pin */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 0);
		/* Reconfigure and reenable it. */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM0, 0); /* level sensitive */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM0, 1);  /* wake on low */
		/* enable powerdown exit */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 1);
	}
}

static void init_interrupts(void)
{
	int i;
	uint32_t exiten = GREG32(PINMUX, EXITEN0);

	/* Clear wake pin interrupts */
	GREG32(PINMUX, EXITEN0) = 0;
	GREG32(PINMUX, EXITEN0) = exiten;

	/* Enable all GPIO interrupts */
	for (i = 0; i < gpio_ih_count; i++)
		if (gpio_list[i].flags & GPIO_INT_ANY)
			gpio_enable_interrupt(i);
}
static void deferred_tpm_rst_isr(void);
DECLARE_DEFERRED(deferred_tpm_rst_isr);

static void configure_board_specific_gpios(void)
{
	/* Add a pullup to sys_rst_l */
	if (board_rst_pullup_needed())
		GWRITE_FIELD(PINMUX, DIOM0_CTL, PU, 1);

	/*
	 * Connect either plt_rst_l or sys_rst_l to GPIO_TPM_RST_L based on the
	 * board type. This signal is used to monitor AP resets and reset the
	 * TPM.
	 *
	 * Also configure these pins to be wake triggers on the rising edge,
	 * this will apply to regular sleep only, entering deep sleep would
	 * reconfigure this.
	 *
	 * plt_rst_l is on diom3, and sys_rst_l is on diom0.
	 */
	if (board_use_plt_rst()) {
		/* Use plt_rst_l for device detect purposes. */
		device_states[DEVICE_AP].detect = GPIO_TPM_RST_L;

		/* Use plt_rst_l as the tpm reset signal. */
		GWRITE(PINMUX, GPIO1_GPIO0_SEL, GC_PINMUX_DIOM3_SEL);

		/* No interrupts from AP UART TX state change are needed. */
		gpio_disable_interrupt(GPIO_DETECT_AP);

		/* Enbale the input */
		GWRITE_FIELD(PINMUX, DIOM3_CTL, IE, 1);

		/*
		 * Make plt_rst_l routed to DIOM3 a low level sensitive wake
		 * source. This way when a plt_rst_l pulse comes along while
		 * H1 is in sleep, the H1 wakes from sleep first, enabling all
		 * necessary clocks, and becomes ready to generate an
		 * interrupt on the rising edge of plt_rst_l.
		 *
		 * It takes at most 150 us to wake up, and the pulse is at
		 * least 1ms long.
		 */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM3, 0);
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM3, 1);

		/* Enable powerdown exit on DIOM3 */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 1);
	} else {
		/* Use AP UART TX for device detect purposes. */
		device_states[DEVICE_AP].detect = GPIO_DETECT_AP;

		/* Use sys_rst_l as the tpm reset signal. */
		GWRITE(PINMUX, GPIO1_GPIO0_SEL, GC_PINMUX_DIOM0_SEL);
		/* Enbale the input */
		GWRITE_FIELD(PINMUX, DIOM0_CTL, IE, 1);

		/* Use AP UART TX as the DETECT AP signal. */
		GWRITE(PINMUX, GPIO1_GPIO1_SEL, GC_PINMUX_DIOA3_SEL);
		/* Enbale the input */
		GWRITE_FIELD(PINMUX, DIOA3_CTL, IE, 1);

		/* Set to be level sensitive */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM0, 0);
		/* wake on low */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM0, 1);
		/* Enable powerdown exit on DIOM0 */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 1);
	}
	/*
	 * If the TPM_RST_L signal is already high when cr50 wakes up or
	 * transitions to high before we are able to configure the gpio then
	 * we will have missed the edge and the tpm reset isr will not get
	 * called. Check that we haven't already missed the rising edge. If we
	 * have alert tpm_rst_isr.
	 */
	if (gpio_get_level(GPIO_TPM_RST_L))
		hook_call_deferred(&deferred_tpm_rst_isr_data, 0);
}

void decrement_retry_counter(void)
{
	uint32_t counter = GREG32(PMU, LONG_LIFE_SCRATCH0);

	if (counter) {
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 1);
		GREG32(PMU, LONG_LIFE_SCRATCH0) = counter - 1;
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 0);
	}
}

/* Initialize board. */
static void board_init(void)
{
	/*
	 * Deep sleep resets should be considered valid and should not impact
	 * the rolling reboot count.
	 */
	if (system_get_reset_flags() & RESET_FLAG_HIBERNATE)
		decrement_retry_counter();
	configure_board_specific_gpios();
	init_pmu();
	init_interrupts();
	init_trng();
	init_jittery_clock(1);
	init_runlevel(PERMISSION_MEDIUM);
	/* Initialize NvMem partitions */
	nvmem_init();
	/* Initialize the persistent storage. */
	initvars();

	system_update_rollback_mask();

	/* Indication that firmware is running, for debug purposes. */
	GREG32(PMU, PWRDN_SCRATCH16) = 0xCAFECAFE;

	/* Enable battery cutoff software support on detachable devices. */
	if (system_battery_cutoff_support_required())
		set_up_battery_cutoff_monitor();

	/*
	 * The interrupt is enabled by default, but we only want it enabled when
	 * bit banging mode is active.
	 */
	gpio_disable_interrupt(GPIO_EC_TX_CR50_RX);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#if defined(CONFIG_USB)
const void * const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Cr50"),
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
	[USB_STR_BLOB_NAME] = USB_STRING_DESC("Blob"),
	[USB_STR_HID_KEYBOARD_NAME] = USB_STRING_DESC("PokeyPokey"),
	[USB_STR_AP_NAME] = USB_STRING_DESC("AP"),
	[USB_STR_EC_NAME] = USB_STRING_DESC("EC"),
	[USB_STR_UPGRADE_NAME] = USB_STRING_DESC("Firmware upgrade"),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("AP EC upgrade"),
	[USB_STR_SERIALNO] = USB_STRING_DESC(DEFAULT_SERIALNO),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
};
BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);
#endif

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	[CONFIG_SPI_FLASH_PORT] = {0, 2, GPIO_COUNT}
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

int flash_regions_to_enable(struct g_flash_region *regions,
			    int max_regions)
{
	/*
	 * This needs to account for two regions: the "other" RW partition and
	 * the NVRAM in TOP_B.
	 *
	 * When running from RW_A the two regions are adjacent, but it is
	 * simpler to keep function logic the same and always configure two
	 * separate regions.
	 */

	if (max_regions < 3)
		return 0;

	/* Enable access to the other RW image... */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
		/* Running RW_A, enable RW_B */
		regions[0].reg_base = CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_RW_B_MEM_OFF;
	else
		/* Running RW_B, enable RW_A */
		regions[0].reg_base = CONFIG_MAPPED_STORAGE_BASE +
			CONFIG_RW_MEM_OFF;
	/* Size is the same */
	regions[0].reg_size = CONFIG_RW_SIZE;
	regions[0].reg_perms = FLASH_REGION_EN_ALL;

	/* Enable access to the NVRAM partition A region */
	regions[1].reg_base = CONFIG_MAPPED_STORAGE_BASE +
		CFG_TOP_A_OFF;
	regions[1].reg_size = CFG_TOP_SIZE;
	regions[1].reg_perms = FLASH_REGION_EN_ALL;

	/* Enable access to the NVRAM partition B region */
	regions[2].reg_base = CONFIG_MAPPED_STORAGE_BASE +
		CFG_TOP_B_OFF;
	regions[2].reg_size = CFG_TOP_SIZE;
	regions[2].reg_perms = FLASH_REGION_EN_ALL;

	return 3;
}

static void deferred_tpm_rst_isr(void)
{
	ccprintf("%T %s\n", __func__);

	if (board_use_plt_rst() &&
	    device_state_changed(DEVICE_AP, DEVICE_STATE_ON))
		hook_notify(HOOK_CHIPSET_RESUME);

	/*
	 * If no reboot request is posted, OR if the other RW's header is not
	 * ready to run - do not try rebooting the device, just reset the
	 * TPM.
	 *
	 * The inactive header will have to be restored by the appropriate
	 * vendor command, the device will be rebooted then.
	 */
	if (!reboot_request_posted || other_rw_is_inactive()) {
		/* Reset TPM, no need to wait for completion. */
		tpm_reset_request(0, 0);
		return;
	}

	/*
	 * Reset TPM and wait to completion to make sure nvmem is
	 * committed before reboot.
	 */
	tpm_reset_request(1, 0);

	/* This will never return. */
	system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD);
}

/* This is the interrupt handler to react to TPM_RST_L */
void tpm_rst_deasserted(enum gpio_signal signal)
{
	hook_call_deferred(&deferred_tpm_rst_isr_data, 0);
}

void assert_sys_rst(void)
{
	/*
	 * We don't have a good (any?) way to easily look up the pinmux/gpio
	 * assignments in gpio.inc, so they're hard-coded in this routine. This
	 * assertion is just to ensure it hasn't changed.
	 */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO4_SEL) == GC_PINMUX_DIOM0_SEL);

	/* Set SYS_RST_L_OUT as an output, connected to the pad */
	GWRITE(PINMUX, DIOM0_SEL, GC_PINMUX_GPIO0_GPIO4_SEL);
	gpio_set_flags(GPIO_SYS_RST_L_OUT, GPIO_OUT_HIGH);

	/* Assert it */
	gpio_set_level(GPIO_SYS_RST_L_OUT, 0);
}

void deassert_sys_rst(void)
{
	ASSERT(GREAD(PINMUX, GPIO0_GPIO4_SEL) == GC_PINMUX_DIOM0_SEL);

	/* Deassert SYS_RST_L */
	gpio_set_level(GPIO_SYS_RST_L_OUT, 1);

	/* Set SYS_RST_L_OUT as an input, disconnected from the pad */
	gpio_set_flags(GPIO_SYS_RST_L_OUT, GPIO_INPUT);
	GWRITE(PINMUX, DIOM0_SEL, 0);
}

int is_sys_rst_asserted(void)
{
	return (GREAD(PINMUX, DIOM0_SEL) == GC_PINMUX_GPIO0_GPIO4_SEL)
#ifdef CONFIG_CMD_GPIO_EXTENDED
		&& (gpio_get_flags(GPIO_SYS_RST_L_OUT) & GPIO_OUTPUT)
#endif
		&& (gpio_get_level(GPIO_SYS_RST_L_OUT) == 0);
}

void assert_ec_rst(void)
{
	GWRITE(RBOX, ASSERT_EC_RST, 1);
}
void deassert_ec_rst(void)
{
	GWRITE(RBOX, ASSERT_EC_RST, 0);
}

int is_ec_rst_asserted(void)
{
	return GREAD(RBOX, ASSERT_EC_RST);
}

static int device_state_changed(enum device_type device,
				enum device_state state)
{
	hook_call_deferred(device_states[device].deferred, -1);
	return device_set_state(device, state);
}

/*
 * If the UART is enabled we cant tell anything about the
 * servo state, so disable servo detection.
 */
static int servo_state_unknown(void)
{
	if (uartn_enabled(UART_EC)) {
		device_set_state(DEVICE_SERVO, DEVICE_STATE_UNKNOWN);
		return 1;
	}
	return 0;
}

static void enable_uart(int uart)
{
	/*
	 * For the EC UART, we can't connect the TX pin to the UART block when
	 * it's in bit bang mode.
	 */
	if ((uart == UART_EC) && uart_bitbang_is_enabled(uart))
		return;

	/* Enable RX and TX on the UART peripheral */
	uartn_enable(uart);

	/* Connect the TX pin to the UART TX Signal */
	if (!uartn_enabled(uart))
		uartn_tx_connect(uart);
}

static void disable_uart(int uart)
{
	/* Disable RX and TX on the UART peripheral */
	uartn_disable(uart);

	/* Disconnect the TX pin from the UART peripheral */
	uartn_tx_disconnect(uart);
}

static int device_powered_off(enum device_type device)
{
	if (device_get_state(device) == DEVICE_STATE_ON)
		return EC_ERROR_UNKNOWN;

	if (!device_state_changed(device, DEVICE_STATE_OFF))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static void servo_deferred(void)
{
	if (servo_state_unknown())
		return;

	/*
	 * If servo was detached reconnect the AP uart making it read write
	 * again.
	 */
	if (device_powered_off(DEVICE_SERVO) == EC_SUCCESS)
		uartn_tx_connect(UART_AP);
}
DECLARE_DEFERRED(servo_deferred);

static void ap_deferred(void)
{
	if (device_powered_off(DEVICE_AP) == EC_SUCCESS)
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
}
DECLARE_DEFERRED(ap_deferred);

static void ec_deferred(void)
{
	if (device_powered_off(DEVICE_EC) == EC_SUCCESS)
		disable_uart(UART_EC);
}
DECLARE_DEFERRED(ec_deferred);

struct device_config device_states[] = {
	[DEVICE_SERVO] = {
		.deferred = &servo_deferred_data,
		.detect = GPIO_DETECT_SERVO,
		.name = "Servo"
	},
	[DEVICE_AP] = {
		.deferred = &ap_deferred_data,
		.name = "AP"
	},
	[DEVICE_EC] = {
		.deferred = &ec_deferred_data,
		.detect = GPIO_DETECT_EC,
		.name = "EC"
	},
	[DEVICE_BATTERY_PRESENT] = {
		.deferred = NULL,
		.detect = GPIO_BATT_PRES_L,
		.name = "BattPrsnt"
	},
	[DEVICE_CCD_MODE] = {
		.deferred = NULL,
		.detect = GPIO_CCD_MODE_L,
		.name = "CCD Mode"
	},
};
BUILD_ASSERT(ARRAY_SIZE(device_states) == DEVICE_COUNT);

static void servo_attached(void)
{
	if (servo_state_unknown())
		return;

#ifdef CONFIG_UART_BITBANG
	uart_bitbang_disable(bitbang_config.uart);
#endif /* defined(CONFIG_UART_BITBANG) */

	/* Update the device state */
	device_state_changed(DEVICE_SERVO, DEVICE_STATE_ON);

	/* Disconnect AP and EC UART when servo is attached */
	uartn_tx_disconnect(UART_AP);
	uartn_tx_disconnect(UART_EC);

	/* Disconnect i2cm interface to ina */
	usb_i2c_board_disable();
}

void device_state_on(enum gpio_signal signal)
{
	/*
	 * On boards with plt_rst_l the ap state is detected with tpm_rst_l.
	 * Make sure we don't disable the tpm reset interrupt.
	 */
	if (signal != GPIO_TPM_RST_L)
		gpio_disable_interrupt(signal);

	switch (signal) {
	case GPIO_TPM_RST_L:
		/*
		 * Boards using tpm_rst_l have no AP state interrupt that will
		 * trigger device_state_on, so this will only get called when we
		 * poll the AP state and see that the detect signal is high, but
		 * the device state is not 'on'.
		 *
		 * Boards using tpm_rst_l to detect the AP state use the tpm
		 * reset handler to set the AP state to 'on'. If we managed to
		 * get to this point, the tpm reset handler has not run yet.
		 * This should only happen if there is a race between the board
		 * state polling and a scheduled call to
		 * deferred_tpm_rst_isr_data, but it may be because we missed
		 * the rising edge. Notify the handler again just in case we
		 * missed the edge to make sure we reset the tpm and update the
		 * state. If there is already a pending call, then this call
		 * won't affect it, because subsequent calls to to
		 * hook_call_deferred just change the delay for the call, and we
		 * are setting the delay to asap.
		 */
		CPRINTS("%s: tpm_rst_isr hasn't set the AP state to 'on'.",
			__func__);
		hook_call_deferred(&deferred_tpm_rst_isr_data, 0);
		break;
	case GPIO_DETECT_AP:
		if (device_state_changed(DEVICE_AP, DEVICE_STATE_ON))
			hook_notify(HOOK_CHIPSET_RESUME);
		break;
	case GPIO_DETECT_EC:
		if (device_state_changed(DEVICE_EC, DEVICE_STATE_ON) &&
		    !uart_bitbang_is_enabled(UART_EC))
			enable_uart(UART_EC);
		break;
	case GPIO_DETECT_SERVO:
		servo_attached();
		break;
	default:
		CPRINTS("Device %d not supported", signal);
		return;
	}
}

void board_update_device_state(enum device_type device)
{
	if (device == DEVICE_BATTERY_PRESENT) {
		/* The battery presence pin is active low. */
		int bp = !gpio_get_level(device_states[device].detect);

		/*
		 * We use BATT_PRES_L as the source for write protect.  However,
		 * since it can be overridden by a console command, only change
		 * the write protect state when the battery presence pin has
		 * changed and we're not forcing it.
		 */
		if (device_set_state(device,
				     bp ?
				     DEVICE_STATE_ON : DEVICE_STATE_OFF)) {
			CPRINTS("battery %spresent", bp ? "" : "NOT ");

			/*
			 * Only update the write protect state if we're not
			 * forcing it.
			 */
			if ((GREG32(PMU, LONG_LIFE_SCRATCH1) & BOARD_FORCING_WP)
			    == 0)
				set_wp_state(bp);
		}
		return;
	}

	if (device == DEVICE_CCD_MODE) {
		int pin_level = gpio_get_level(device_states[device].detect);
		/* The CCD mode pin is active low. */
		int changed = device_set_state(device,
					       pin_level ?
					       DEVICE_STATE_OFF :
					       DEVICE_STATE_ON);

		if (changed) {
			CPRINTS("CCD MODE changed: %d", pin_level);
			ccd_mode_pin_changed(pin_level);
		}

		return;
	}

	if (device == DEVICE_SERVO && servo_state_unknown())
		return;

	/*
	 * If the device is currently on set its state immediately. If it
	 * thinks the device is powered off debounce the signal.
	 */
	if (gpio_get_level(device_states[device].detect)) {
		if (device_get_state(device) == DEVICE_STATE_ON)
			return;
		device_state_on(device_states[device].detect);
	} else {
		if (device_get_state(device) == DEVICE_STATE_OFF)
			return;
		device_set_state(device, DEVICE_STATE_UNKNOWN);
		if ((device != DEVICE_AP) || !board_use_plt_rst())
			gpio_enable_interrupt(device_states[device].detect);

		/*
		 * The signal is low now, but this could be just an AP UART
		 * transmitting or PLT_RST_L pulsing. Let's wait long enough
		 * to debounce in both cases, pickng duration slightly shorter
		 * than the device polling iterval.
		 *
		 * Interrupts from the appropriate source (platform dependent)
		 * will cancel the deferred function if the signal is
		 * deasserted within the deferral interval.
		 */
		hook_call_deferred(device_states[device].deferred, 900 * MSEC);
	}
}

static void ap_shutdown(void)
{
	/*
	 * If I2C TPM is configured then the INT_AP_L signal is used as
	 * a low pulse trigger to sync I2C transactions with the
	 * host. By default Cr50 is driving this line high, but when the
	 * AP powers off, the 1.8V rail that it's pulled up to will be
	 * off and cause exessive power to be consumed by the Cr50. Set
	 * INT_AP_L as an input while the AP is powered off.
	 */
	gpio_set_flags(GPIO_INT_AP_L, GPIO_INPUT);

	disable_uart(UART_AP);

	/*
	 * We don't enable deep sleep on ARM devices yet, as its processing
	 * there will require more support on the AP side than is available
	 * now.
	 */
	if (board_use_plt_rst())
		enable_deep_sleep();
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, ap_shutdown, HOOK_PRIO_DEFAULT);

static void ap_resume(void)
{
	/*
	 * AP is powering up, set the I2C host sync signal to output and set
	 * it high which is the default level.
	 */
	gpio_set_flags(GPIO_INT_AP_L, GPIO_OUT_HIGH);
	gpio_set_level(GPIO_INT_AP_L, 1);

	enable_uart(UART_AP);

	disable_deep_sleep();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, ap_resume, HOOK_PRIO_DEFAULT);

/*
 * This function duplicates some of the functionality in chip/g/gpio.c in order
 * to configure a given strap pin to be either a low gpio output, a gpio input
 * with or without an internal pull resistor, or disconnect the gpio signal
 * from the pin pad.
 *
 * The desired gpio functionality is contained in the input parameter flags,
 * while the strap parameter is an index into the array strap_regs.
 */
static void strap_config_pin(enum strap_list strap, int flags)
{
	const struct gpio_info *g = gpio_list + strap_regs[strap].gpio_signal;
	int bitnum  = GPIO_MASK_TO_NUM(g->mask);
	int mask = DIO_CTL_IE_MASK | DIO_CTL_PD_MASK | DIO_CTL_PU_MASK;
	int val;

	if (!flags) {
		/* Reset strap pins, disconnect output and clear pull up/dn */
		/* Disconnect gpio from pin mux */
		DIO_SEL_REG(strap_regs[strap].sel_offset) = 0;
		/* Clear input enable and pulldown/pullup in pinmux */
		REG_WRITE_MLV(DIO_CTL_REG(strap_regs[strap].sel_offset),
			      mask, 0, 0);
		return;
	}

	if (flags & GPIO_OUT_LOW) {
		/* Config gpio to output and drive low */
		gpio_set_flags(strap_regs[strap].gpio_signal, GPIO_OUT_LOW);
		/* connect pin mux to gpio */
		DIO_SEL_REG(strap_regs[strap].sel_offset) =
			GET_GPIO_FUNC(g->port, bitnum);
		return;
	}

	if (flags & GPIO_INPUT) {
		/* Configure gpio pin to be an input */
		gpio_set_flags(strap_regs[strap].gpio_signal, GPIO_INPUT);
		/* Connect pad to gpio */
		GET_GPIO_SEL_REG(g->port, bitnum) =
			strap_regs[strap].pad_select;

		/*
		 * Input enable is bit 2 of the CTL register. Pulldown enable is
		 * bit 3, and pullup enable is bit 4. Always set input enable
		 * and clear the pullup/pulldown bits unless the flags variable
		 * specifies that pulldown or pullup should be enabled.
		 */
		val = DIO_CTL_IE_MASK;
		if (flags & GPIO_PULL_DOWN)
			val |= DIO_CTL_PD_MASK;
		if (flags & GPIO_PULL_UP)
			val |= DIO_CTL_PU_MASK;
		/* Set input enable and pulldown/pullup in pinmux */
		REG_WRITE_MLV(DIO_CTL_REG(strap_regs[strap].sel_offset),
			      mask, 0, val);
	}
}

static int get_strap_config(uint8_t *config)
{
	enum strap_list s0;
	int lvl;
	int flags;
	uint8_t pull_a;
	uint8_t pull_b;

	/*
	 * There are 4 pins that are used to determine Cr50 board strapping
	 * options. These pins are:
	 *   1. DIOA1  -> I2CS_SDA
	 *   2. DI0A9  -> I2CS_SCL
	 *   3. DIOA6  -> SPS_CLK
	 *   4. DIOA12 -> SPS_CS_L
	 * There are two main configuration options based on whether I2C or SPI
	 * is used for TPM2 communication to/from the host AP. If SPI is the
	 * TPM2 bus, then the pair of pins DIOA9|DIOA1 are used to designate
	 * strapping options. If TPM uses I2C, then DIOA12|DIOA6 are the
	 * strapping pins.
	 *
	 * Each strapping pin will have either an external pullup or pulldown
	 * resistor. The external pull resistors have two levels, 5k for strong
	 * and 1M for weak. Cr50 has internal pullup/pulldown 50k resistors that
	 * can be configured via pinmux register settings. This combination of
	 * external and internal pullup/pulldown resistors allows for 4 possible
	 * states per strapping pin. The following table shows the different
	 * combinations. Note that when a strong external pull down/up resistor
	 * is used, the internal resistor is a don't care and those cases are
	 * marked by n/a. The bits column represents the signal level read on
	 * the gpio pin. Bit 1 of this field is the value read with the internal
	 * pull down/up resistors disabled, and bit 0 is the gpio signal level
	 * of the same pin when the internal pull resistor is selected as shown
	 * in the 'internal' column.
	 *   external    internal   bits
	 *   --------    --------   ----
	 *    5K PD       n/a        00
	 *    1M PD       50k PU     01
	 *    1M PU       50k PD     10
	 *    5K PU       n/a        11
	 *
	 * To determine the bits associated with each strapping pin, the
	 * following method is used.
	 *   1. Set all 4 pins as inputs with internal pulls disabled.
	 *   2. For each pin do the following to encode 2 bits b1:b0
	 *      a. b1 = gpio_get_level(pin)
	 *      b. If b1 == 1, then enable internal pulldown, else enable
	 *         internal pullup resistor.
	 *      c. b0 = gpio_get_level(pin)
	 *
	 * To be considered a valid strap configuraiton, the upper 4 bits must
	 * have no pullups and at least one pullup in the lower 4 bits or vice
	 * versa. So can use 0xA0 and 0x0A as masks to check for each
	 * condition. Once this check is passed, the 4 bits which are used to
	 * distinguish between SPI vs I2C are masked since reading them as weak
	 * pulldowns is not being explicitly required due to concerns that the
	 * AP could prevent accurate differentiation between strong and weak
	 * pull down cases.
	 */

	/* Drive all 4 strap pins low to discharge caps. */
	for (s0 = a0; s0 < ARRAY_SIZE(strap_regs); s0++)
		strap_config_pin(s0, GPIO_OUT_LOW);
	/* Delay long enough to discharge any caps. */
	udelay(STRAP_PIN_DELAY_USEC);

	/* Set all 4 strap pins as inputs with pull resistors disabled. */
	for (s0 = a0; s0 < ARRAY_SIZE(strap_regs); s0++)
		strap_config_pin(s0, GPIO_INPUT);
	/* Delay so voltage levels can settle. */
	udelay(STRAP_PIN_DELAY_USEC);

	*config = 0;
	/* Read 2 bit value of each strapping pin. */
	ccprintf("strap pin readings:");
	for (s0 = a0; s0 < ARRAY_SIZE(strap_regs); s0++) {
		lvl = gpio_get_level(strap_regs[s0].gpio_signal);
		flags = GPIO_INPUT;
		if (lvl)
			flags |= GPIO_PULL_DOWN;
		else
			flags |= GPIO_PULL_UP;
		/* Enable internal pull down/up resistor. */
		strap_config_pin(s0, flags);
		udelay(STRAP_PIN_DELAY_USEC);
		lvl = (lvl << 1) |
			gpio_get_level(strap_regs[s0].gpio_signal);
		ccprintf(" %s:%d", strap_regs[s0].pad_name, lvl);
		*config |= lvl << s0 * 2;

		/*
		 * Finished with this pin. Disable internal pull up/dn resistor
		 * and disconnect gpio from pin mux. The pins used for straps
		 * are configured for their desired role when either the SPI or
		 * I2C interfaces are initialized.
		 */
		strap_config_pin(s0, 0);
	}
	ccprintf("\n");

	/*
	 * The strap bits for DIOA12|DIOA6 are in the upper 4 bits of 'config'
	 * while the strap bits for DIOA9|DIOA1 are in the lower 4 bits. Check
	 * for SPI vs I2C config by checking for presence of external pullups in
	 * one group of 4 bits and confirming no external pullups in the other
	 * group. For SPI config the weak pulldowns may not be accurately read
	 * on DIOA12|DIOA6 and similarly for I2C config on
	 * DIOA9|DIOA1. Therefore, only requiring that there be no external
	 * pullups on these pins and will mask the bits so they will match the
	 * config table entries.
	 */

	pull_a = *config & 0xa0;
	pull_b = *config & 0xa;
	if ((!pull_a && !pull_b) || (pull_a && pull_b))
		return EC_ERROR_INVAL;

	/* Now that I2C vs SPI is known, mask the unused strap bits. */
	*config &= *config & 0xa ? 0xf : 0xf0;

	return EC_SUCCESS;
}

static uint32_t get_properties(void)
{
	int i;
	uint8_t config;
	uint32_t properties;

	if (get_strap_config(&config) != EC_SUCCESS) {
		/*
		 * No pullups were detected on any of the strap pins so there
		 * is no point in checking for a matching config table entry.
		 * For this case use default properties.
		 */
		CPRINTS("Invalid strap pins! Default properties = 0x%x",
			BOARD_PROPERTIES_DEFAULT);
		return BOARD_PROPERTIES_DEFAULT;
	}

	/* Search board config table to find a matching entry */
	for (i = 0; i < ARRAY_SIZE(board_cfg_table); i++) {
		if (board_cfg_table[i].strap_cfg == config) {
			properties = board_cfg_table[i].board_properties;
			CPRINTS("Valid strap: 0x%x properties: 0x%x",
				config, properties);
			/* Read board properties for this config */
			return properties;
		}
	}

	/*
	 * Reached the end of the table and didn't find a matching config entry.
	 * However, the SPI vs I2C determination can still be made as
	 *get_strap_config() returned EC_SUCCESS.
	 */
	properties = config & 0xa ? BOARD_SLAVE_CONFIG_SPI :
		BOARD_PROPERTIES_DEFAULT;
	CPRINTS("strap_cfg 0x%x has no table entry, prop = 0x%x",
		config, properties);
	return properties;
}

static void init_board_properties(void)
{
	uint32_t properties;

	properties = GREG32(PMU, LONG_LIFE_SCRATCH1);

	/*
	 * This must be a power on reset or maybe restart due to a software
	 * update from a version not setting the register.
	 */
	if (!(properties & BOARD_ALL_PROPERTIES) || (system_get_reset_flags() &
						     RESET_FLAG_HARD)) {
		/*
		 * Mask board properties because following hard reset, they
		 * won't be cleared.
		 */
		properties &= ~BOARD_ALL_PROPERTIES;
		properties |= get_properties();
		/*
		 * Now save the properties value for future use.
		 *
		 * Enable access to LONG_LIFE_SCRATCH1 reg.
		 */
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);
		/* Save properties in LONG_LIFE register */
		GREG32(PMU, LONG_LIFE_SCRATCH1) = properties;
		/* Disable access to LONG_LIFE_SCRATCH1 reg */
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);
	}
	/* Save this configuration setting */
	board_properties = properties;
}
DECLARE_HOOK(HOOK_INIT, init_board_properties, HOOK_PRIO_FIRST);

void i2cs_set_pinmux(void)
{
	/* Connect I2CS SDA/SCL output to A1/A9 pads */
	GWRITE(PINMUX, DIOA1_SEL, GC_PINMUX_I2CS0_SDA_SEL);
	GWRITE(PINMUX, DIOA9_SEL, GC_PINMUX_I2CS0_SCL_SEL);
	/* Connect A1/A9 pads to I2CS input SDA/SCL */
	GWRITE(PINMUX, I2CS0_SDA_SEL, GC_PINMUX_DIOA1_SEL);
	GWRITE(PINMUX, I2CS0_SCL_SEL, GC_PINMUX_DIOA9_SEL);
	/* Enable SDA/SCL inputs from A1/A9 pads */
	GWRITE_FIELD(PINMUX, DIOA1_CTL, IE, 1);	 /* I2CS_SDA */
	GWRITE_FIELD(PINMUX, DIOA9_CTL, IE, 1);	 /* I2CS_SCL */

	/* Allow I2CS_SCL to wake from sleep */
	GWRITE_FIELD(PINMUX, EXITEDGE0, DIOA9, 1); /* edge sensitive */
	GWRITE_FIELD(PINMUX, EXITINV0, DIOA9, 1);  /* wake on low */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA9, 1);   /* enable powerdown exit */

	/* Allow I2CS_SDA to wake from sleep */
	GWRITE_FIELD(PINMUX, EXITEDGE0, DIOA1, 1); /* edge sensitive */
	GWRITE_FIELD(PINMUX, EXITINV0, DIOA1, 1);  /* wake on low */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA1, 1);   /* enable powerdown exit */
}

/* Determine key type based on the key ID. */
static const char *key_type(uint32_t key_id)
{

	/*
	 * It is a mere convention, but all prod keys are required to have key
	 * IDs such, that bit D2 is set, and all dev keys are required to have
	 * key IDs such, that bit D2 is not set.
	 *
	 * This convention is enforced at the key generation time.
	 */
	if (key_id & (1 << 2))
		return "prod";
	else
		return "dev";
}

static int command_sysinfo(int argc, char **argv)
{
	enum system_image_copy_t active;
	uintptr_t vaddr;
	const struct SignedHeader *h;
	int reset_count = GREG32(PMU, LONG_LIFE_SCRATCH0);
	char rollback_str[15];

	ccprintf("Reset flags: 0x%08x (", system_get_reset_flags());
	system_print_reset_flags();
	ccprintf(")\n");
	if (reset_count > 6)
		ccprintf("Rollback detected\n");
	ccprintf("Reset count: %d\n", reset_count);

	ccprintf("Chip:        %s %s %s\n", system_get_chip_vendor(),
		 system_get_chip_name(), system_get_chip_revision());

	active = system_get_ro_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	ccprintf("RO keyid:    0x%08x(%s)\n", h->keyid, key_type(h->keyid));

	active = system_get_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	ccprintf("RW keyid:    0x%08x(%s)\n", h->keyid, key_type(h->keyid));

	ccprintf("DEV_ID:      0x%08x 0x%08x\n",
		 GREG32(FUSE, DEV_ID0), GREG32(FUSE, DEV_ID1));

	system_get_rollback_bits(rollback_str, sizeof(rollback_str));
	ccprintf("Rollback:    %s\n", rollback_str);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(sysinfo, command_sysinfo,
			     NULL,
			     "Print system info");

/*
 * SysInfo command:
 * There are no input args.
 * Output is this struct, all fields in network order.
 */
struct sysinfo_s {
	uint32_t ro_keyid;
	uint32_t rw_keyid;
	uint32_t dev_id0;
	uint32_t dev_id1;
} __packed;

static enum vendor_cmd_rc vc_sysinfo(enum vendor_cmd_cc code,
				     void *buf,
				     size_t input_size,
				     size_t *response_size)
{
	enum system_image_copy_t active;
	uintptr_t vaddr;
	const struct SignedHeader *h;
	struct sysinfo_s *sysinfo = buf;

	active = system_get_ro_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	sysinfo->ro_keyid = htobe32(h->keyid);

	active = system_get_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	sysinfo->rw_keyid = htobe32(h->keyid);

	sysinfo->dev_id0 = htobe32(GREG32(FUSE, DEV_ID0));
	sysinfo->dev_id1 = htobe32(GREG32(FUSE, DEV_ID1));

	*response_size = sizeof(*sysinfo);
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SYSINFO, vc_sysinfo);

static enum vendor_cmd_rc vc_invalidate_inactive_rw(enum vendor_cmd_cc code,
						    void *buf,
						    size_t input_size,
						    size_t *response_size)
{
	const struct SignedHeader *header;
	uint32_t ctrl;
	uint32_t base_addr;
	uint32_t size;
	const char zero[4] = {}; /* value to write to magic. */

	*response_size = 0;

	if (other_rw_is_inactive()) {
		CPRINTS("%s: Inactive region is disabled", __func__);
		return VENDOR_RC_SUCCESS;
	}

	/* save the original flash region6 register values */
	ctrl = GREAD(GLOBALSEC, FLASH_REGION6_CTRL);
	base_addr = GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR);
	size = GREG32(GLOBALSEC, FLASH_REGION6_SIZE);

	header = get_other_rw_addr();

	/* Enable RW access to the other header. */
	GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR) = (uint32_t) header;
	GREG32(GLOBALSEC, FLASH_REGION6_SIZE) = 1023;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, WR_EN, 1);

	CPRINTS("%s: TPM verified corrupting inactive image, magic before %x",
		__func__, header->magic);

	flash_physical_write((intptr_t)&header->magic -
			     CONFIG_PROGRAM_MEMORY_BASE,
			     sizeof(zero), zero);

	CPRINTS("%s: magic after: %x", __func__, header->magic);

	/* Restore original values */
	GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR) = base_addr;
	GREG32(GLOBALSEC, FLASH_REGION6_SIZE) = size;
	GREG32(GLOBALSEC, FLASH_REGION6_CTRL) = ctrl;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_INVALIDATE_INACTIVE_RW,
	vc_invalidate_inactive_rw);

static enum vendor_cmd_rc vc_commit_nvmem(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	nvmem_enable_commits();
	*response_size = 0;
	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_COMMIT_NVMEM, vc_commit_nvmem);

static int command_board_properties(int argc, char **argv)
{
	ccprintf("properties = 0x%x\n", board_properties);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(brdprop, command_board_properties,
			     NULL, "Display board properties");
