/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "board_id.h"
#include "ccd_config.h"
#include "clock.h"
#include "closed_source_set1.h"
#include "common.h"
#include "console.h"
#include "dcrypto/dcrypto.h"
#include "ec_version.h"
#include "endian.h"
#include "extension.h"
#include "flash.h"
#include "flash_config.h"
#include "gpio.h"
#include "ite_sync.h"
#include "hooks.h"
#include "i2c.h"
#include "i2cs.h"
#include "init_chip.h"
#include "nvmem.h"
#include "nvmem_vars.h"
#include "rbox.h"
#include "rdd.h"
#include "recovery_button.h"
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
#include "usart.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_i2c.h"
#include "usb_spi.h"
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

/*  Board specific configuration settings */
static uint32_t board_properties; /* Mainly used as a cache for strap config. */
static uint8_t reboot_request_posted;

/* Which UARTs we'd like to be able to bitbang. */
struct uart_bitbang_properties bitbang_config = {
	.uart = UART_EC,
	.tx_gpio = GPIO_DETECT_SERVO, /* This is TX to EC console. */
	.rx_gpio = GPIO_EC_TX_CR50_RX,
	.rx_irq = GC_IRQNUM_GPIO1_GPIO11INT, /* Must match gpoi.inc */
	/*
	 * The rx/tx_pinmux_regval values MUST agree with the pin config for
	 * both the TX and RX GPIOs in gpio.inc.  Don't change one without
	 * changing the other.
	 */
	.tx_pinmux_reg = GBASE(PINMUX) + GOFFSET(PINMUX, DIOB5_SEL),
	.tx_pinmux_regval = GC_PINMUX_GPIO1_GPIO3_SEL,
	.rx_pinmux_reg = GBASE(PINMUX) + GOFFSET(PINMUX, DIOB6_SEL),
	.rx_pinmux_regval = GC_PINMUX_GPIO1_GPIO11_SEL,
};

DECLARE_IRQ(GC_IRQNUM_GPIO1_GPIO11INT, uart_bitbang_irq, 0);

const char *device_state_names[] = {
	"init",
	"init_debouncing",
	"init_rx_only",
	"disconnected",
	"off",
	"undetectable",
	"connected",
	"on",
	"debouncing",
	"unknown",
	"ignored"
};
BUILD_ASSERT(ARRAY_SIZE(device_state_names) == DEVICE_STATE_COUNT);

const char *device_state_name(enum device_state state)
{
	if (state >= 0 && state < DEVICE_STATE_COUNT)
		return device_state_names[state];
	else
		return "?";
}

int board_use_plt_rst(void)
{
	return !!(board_properties & BOARD_USE_PLT_RESET);
}

/* Allow enabling deep sleep if the board supports it. */
int board_deep_sleep_allowed(void)
{
	return !(board_properties & BOARD_DEEP_SLEEP_DISABLED);
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

int board_uses_closed_source_set1(void)
{
	return !!(board_properties & BOARD_CLOSED_SOURCE_SET1);
}

int board_uses_closed_loop_reset(void)
{
	return !!(board_properties & BOARD_CLOSED_LOOP_RESET);
}

int board_has_ina_support(void)
{
	return !(board_properties & BOARD_NO_INA_SUPPORT);
}

int board_tpm_mode_change_allowed(void)
{
	return !!(board_properties & BOARD_ALLOW_CHANGE_TPM_MODE);
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

#define BOARD_PROPERTIES_DEFAULT (BOARD_SLAVE_CONFIG_I2C | BOARD_USE_PLT_RESET)
static struct board_cfg board_cfg_table[] = {
	/* SPI Variants: DIOA12 = 1M PD, DIOA6 = 1M PD */
	/* Kevin/Gru: DI0A9 = 5k PD, DIOA1 = 1M PU */
	{
		.strap_cfg = 0x02,
		.board_properties = BOARD_SLAVE_CONFIG_SPI |
			BOARD_NEEDS_SYS_RST_PULL_UP,
	},
	/* Poppy: DI0A9 = 1M PU, DIOA1 = 1M PU */
	{
		.strap_cfg = 0x0A,
		.board_properties = BOARD_SLAVE_CONFIG_SPI |
			BOARD_USE_PLT_RESET,
	},
	/* Mistral: DI0A9 = 1M PU, DIOA1 = 5k PU */
	{
		.strap_cfg = 0x0B,
		.board_properties = BOARD_SLAVE_CONFIG_SPI |
			BOARD_USE_PLT_RESET | BOARD_NO_INA_SUPPORT |
			BOARD_CLOSED_LOOP_RESET,
	},
	/* Kukui: DI0A9 = 5k PU, DIOA1 = 5k PU */
	{
		.strap_cfg = 0x0F,
		.board_properties = BOARD_SLAVE_CONFIG_SPI |
			BOARD_USE_PLT_RESET,
	},
	/* I2C Variants: DIOA9 = 1M PD, DIOA1 = 1M PD */
	/* Reef/Eve: DIOA12 = 5k PD, DIOA6 = 1M PU */
	{
		.strap_cfg = 0x20,
		.board_properties = BOARD_SLAVE_CONFIG_I2C |
			BOARD_USE_PLT_RESET,
	},
	/* Rowan: DIOA12 = 5k PD, DIOA6 = 5k PU */
	{
		.strap_cfg = 0x30,
		.board_properties = BOARD_SLAVE_CONFIG_I2C |
			BOARD_DEEP_SLEEP_DISABLED | BOARD_DETECT_AP_WITH_UART,
	},
	/* Sarien/Arcada: DIOA12 = 1M PD, DIOA6 = 5k PU */
	{
		.strap_cfg = 0x70,
		.board_properties = BOARD_SLAVE_CONFIG_I2C |
			BOARD_USE_PLT_RESET | BOARD_WP_DISABLE_DELAY |
			BOARD_CLOSED_SOURCE_SET1 | BOARD_NO_INA_SUPPORT |
			BOARD_ALLOW_CHANGE_TPM_MODE,
	},

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
 * Falling edge indicates AC no longer present (removal of the charger cable)
 * and rising edge indicates AP present (insertion of charger cable).
 */
static void ac_power_state_changed(void)
{
	uint32_t req;
	/* Get current status and clear it. */
	req = GREG32(RBOX, INT_STATE) & (ac_pres_red | ac_pres_fed);
	GREG32(RBOX, INT_STATE) = req;

	CPRINTS("AC: %c%c",
		req & ac_pres_red ? 'R' : '-',
		req & ac_pres_fed ? 'F' : '-');

	/* Delay sleep so RDD state machines can stabilize */
	delay_sleep_by(5 * SECOND);

	/* The remaining code is only used for battery cutoff */
	if (!system_battery_cutoff_support_required())
		return;

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
static void init_ac_detect(void)
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

/**
 * Reset wake logic
 *
 * If any wake pins are edge triggered, the pad logic latches the wakeup. Clear
 * and restore EXITEN0 to reset the wakeup logic.
 */
static void reset_wake_logic(void)
{
	uint32_t exiten = GREG32(PINMUX, EXITEN0);

	GREG32(PINMUX, EXITEN0) = 0;
	GREG32(PINMUX, EXITEN0) = exiten;
}

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
	int wakeup_src;
	static uint8_t count;
	static uint8_t ws;
	static uint8_t line_length;
	static const char wheel[] = { '|', '/', '-', '\\' };

	delay_sleep_by(1 * MSEC);

	wakeup_src = GR_PMU_EXITPD_SRC;

	/* Clear interrupt state */
	GWRITE_FIELD(PMU, INT_STATE, INTR_WAKEUP, 1);

	/* Clear pmu reset */
	GWRITE(PMU, CLRRST, 1);

	/*
	 * This will print the next state of the "rotating wheel" every time
	 * cr50 resumes from regular sleep (8 is the ASCII code for
	 * 'backspace'). Each time wake source changes, its hex value is
	 * printed out preceded by a space.
	 *
	 * In steady state when there is no other activity Cr50 wakes up every
	 * half second for HOOK_TICK, so that is the rate the wheel will be
	 * spinning at when device is idle.
	 */
	if (ws == wakeup_src) {
		ccprintf("%c%c%c%2x%c", 8, 8, 8, ws,
			 wheel[count++ % sizeof(wheel)]);
	} else {
		ws = wakeup_src;
		line_length += 3;
		if (line_length > 50) {
			ccprintf("\n");
			line_length = 0;
		}
		ccprintf(" %2x ", wakeup_src);
	}

	if (wakeup_src & GC_PMU_EXITPD_SRC_RBOX_WAKEUP_MASK)
		rbox_clear_wakeup();

	/* Disable rbox wakeup. It will be reenabled before entering sleep. */
	GREG32(RBOX, WAKEUP) = 0;

	if (wakeup_src & GC_PMU_EXITPD_SRC_PIN_PD_EXIT_MASK) {
		reset_wake_logic();

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
	 * Configure the TPM_RST_L signal as wake on high. There is a
	 * requirement the tpm reset has to remain asserted when cr50 should
	 * be in deep sleep, so cr50 should not wake up until it goes high.
	 *
	 * Whether it is a short pulse or long one waking on the high level is
	 * fine, because the goal of TPM_RST_L is to reset the TPM and after
	 * resuming from deep sleep the TPM will be reset. Cr50 doesn't need to
	 * read the low value and then reset.
	 */
	if (board_use_plt_rst()) {
		/* Configure plt_rst_l to wake on high */
		/* Disable plt_rst_l as a wake pin */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 0);
		/* Reconfigure the pin */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM3, 0); /* level sensitive */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM3, 0);  /* wake on high */
		/* enable powerdown exit */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 1);
	} else {
		/* Configure plt_rst_l to wake on high */
		/* Disable sys_rst_l as a wake pin */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 0);
		/* Reconfigure the pin */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM0, 0); /* level sensitive */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM0, 0);  /* wake on high */
		/* enable powerdown exit */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 1);
	}
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
		/* Use plt_rst_l as the tpm reset signal. */
		/* Select for TPM_RST_L */
		GWRITE(PINMUX, GPIO1_GPIO0_SEL, GC_PINMUX_DIOM3_SEL);
		/* Select for DETECT_TPM_RST_L_ASSERTED */
		GWRITE(PINMUX, GPIO1_GPIO4_SEL, GC_PINMUX_DIOM3_SEL);

		/* Enable the input */
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
		/* Use sys_rst_l as the tpm reset signal. */
		/* Select for TPM_RST_L */
		GWRITE(PINMUX, GPIO1_GPIO0_SEL, GC_PINMUX_DIOM0_SEL);
		/* Select for DETECT_TPM_RST_L_ASSERTED */
		GWRITE(PINMUX, GPIO1_GPIO4_SEL, GC_PINMUX_DIOM0_SEL);
		/* Enable the input */
		GWRITE_FIELD(PINMUX, DIOM0_CTL, IE, 1);

		/* Set to be level sensitive */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM0, 0);
		/* wake on low */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM0, 1);
		/* Enable powerdown exit on DIOM0 */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 1);
	}

	if (board_uses_closed_source_set1())
		closed_source_set1_configure_gpios();
}

static uint8_t mismatched_board_id;

int board_id_is_mismatched(void)
{
	return !!mismatched_board_id;
}

static void  check_board_id_mismatch(void)
{
	if (!board_id_mismatch(NULL))
		return;

	if (system_rollback_detected()) {
		/*
		 * We are in a rollback, the other image must be no good.
		 * Let's keep going with the TPM disabled, only updates will
		 * be allowed.
		 */
		mismatched_board_id = 1;
		ccprintf("Board ID mismatched, but can not reboot.\n");

		/* Force CCD disabled */
		ccd_disable();

		return;
	}

	system_ensure_rollback();
	ccprintf("Rebooting due to board ID mismatch\n");
	cflush();
	system_reset(0);
}

/*
 * Check if ITE SYNC sequence generation was requested before the reset, if so
 * - clear the request and call the function to generate the sequence.
 */
static void maybe_trigger_ite_sync(void)
{
	uint32_t lls1;

	lls1 = GREG32(PMU, LONG_LIFE_SCRATCH1);

	if (!(lls1 & BOARD_ITE_EC_SYNC_NEEDED))
		return;

	/* Clear the sync required bit, this should work only once. */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);
	GREG32(PMU, LONG_LIFE_SCRATCH1) = lls1 & ~BOARD_ITE_EC_SYNC_NEEDED;
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);

	generate_ite_sync();
}

/* Initialize board. */
static void board_init(void)
{
#ifdef CR50_DEV
	static enum ccd_state ccd_init_state = CCD_STATE_OPENED;
#else
	static enum ccd_state ccd_init_state = CCD_STATE_LOCKED;
#endif

	/*
	 * Deep sleep resets should be considered valid and should not impact
	 * the rolling reboot count.
	 */
	if (system_get_reset_flags() & EC_RESET_FLAG_HIBERNATE)
		system_decrement_retry_counter();
	configure_board_specific_gpios();
	init_pmu();
	reset_wake_logic();
	init_trng();
	maybe_trigger_ite_sync();
	init_jittery_clock(1);
	init_runlevel(PERMISSION_MEDIUM);
	/* Initialize NvMem partitions */
	nvmem_init();

	/*
	 * If this was a low power wake and not a rollback, restore the ccd
	 * state from the long-life register.
	 */
	if ((system_get_reset_flags() & EC_RESET_FLAG_HIBERNATE) &&
	    !system_rollback_detected()) {
		ccd_init_state = (GREG32(PMU, LONG_LIFE_SCRATCH1) &
				  BOARD_CCD_STATE) >> BOARD_CCD_SHIFT;
	}

	/* Load case-closed debugging config.  Must be after initvars(). */
	ccd_config_init(ccd_init_state);

	system_update_rollback_mask_with_both_imgs();

	/* Indication that firmware is running, for debug purposes. */
	GREG32(PMU, PWRDN_SCRATCH16) = 0xCAFECAFE;

	/*
	 * Call the function twice to make it harder to glitch execution into
	 * passing the check when not supposed to.
	 */
	check_board_id_mismatch();
	check_board_id_mismatch();

	/*
	 * Start monitoring AC detect to wake Cr50 from deep sleep.  This is
	 * needed to detect RDD cable changes in deep sleep.  AC detect is also
	 * used for battery cutoff software support on detachable devices.
	 */
	init_ac_detect();
	init_rdd_state();

	/* Initialize write protect.  Must be after CCD config init. */
	init_wp_state();

	/*
	 * Need to do this at run time as compile time constant initialization
	 * to a variable value (even to a const known at compile time) is not
	 * supported.
	 */
	bitbang_config.uart_in = ec_uart.producer.queue;

	/*
	 * Enable interrupt handler for RBOX key combo so it can be used to
	 * store the recovery request.
	 */
	if (board_uses_closed_source_set1()) {
		/* Enable interrupt handler for reset button combo */
		task_enable_irq(GC_IRQNUM_RBOX0_INTR_BUTTON_COMBO0_RDY_INT);
		GWRITE_FIELD(RBOX, INT_ENABLE, INTR_BUTTON_COMBO0_RDY, 1);
	}

	/*
	 * Note that the AP, EC, and servo state machines do not have explicit
	 * init_xxx_state() functions, because they don't need to configure
	 * registers prior to starting their state machines.  Their state
	 * machines run in HOOK_SECOND, which first triggers right after
	 * HOOK_INIT, not at +1.0 seconds.
	 */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/**
 * Hook for CCD config loaded/changed.
 */
static void board_ccd_config_changed(void)
{
	/* Store the current CCD state so we can restore it after deep sleep */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);
	GREG32(PMU, LONG_LIFE_SCRATCH1) &= ~BOARD_CCD_STATE;
	GREG32(PMU, LONG_LIFE_SCRATCH1) |= (ccd_get_state() << BOARD_CCD_SHIFT)
			& BOARD_CCD_STATE;
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);

	if (board_uses_closed_source_set1())
		closed_source_set1_update_factory_mode();

	/* Update CCD state */
	ccd_update_state();
}
DECLARE_HOOK(HOOK_CCD_CHANGE, board_ccd_config_changed, HOOK_PRIO_DEFAULT);

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

/**
 * Deferred TPM reset interrupt handling
 *
 * This is always called from the HOOK task.
 */
static void deferred_tpm_rst_isr(void)
{
	CPRINTS("%s", __func__);

	/*
	 * TPM reset is used to detect the AP, connect AP. Let the AP state
	 * machine know the AP is on.
	 */
	set_ap_on();

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

/**
 * Handle TPM_RST_L deasserting
 *
 * This can also be called explicitly from AP detection, if it thinks the
 * interrupt handler missed the rising edge.
 */
void tpm_rst_deasserted(enum gpio_signal signal)
{
	hook_call_deferred(&deferred_tpm_rst_isr_data, 0);
}

void assert_sys_rst(void)
{
	/* Assert it */
	gpio_set_level(GPIO_SYS_RST_L_OUT, 0);
}

void deassert_sys_rst(void)
{
	/* Deassert it */
	gpio_set_level(GPIO_SYS_RST_L_OUT, 1);
}

static int is_sys_rst_asserted(void)
{
	/*
	 * SYS_RST_L is pseudo open drain. It is only an output when it's
	 * asserted.
	 */
	return gpio_get_flags(GPIO_SYS_RST_L_OUT) & GPIO_OUTPUT;
}

/**
 * Reboot the AP
 */
void board_reboot_ap(void)
{
	if (board_uses_closed_loop_reset()) {
		board_closed_loop_reset();
		return;
	}
	assert_sys_rst();
	msleep(20);
	deassert_sys_rst();
}

/**
 * Reboot the EC
 */
void board_reboot_ec(void)
{
	if (board_uses_closed_loop_reset()) {
		board_closed_loop_reset();
		return;
	}
	assert_ec_rst();
	deassert_ec_rst();
}

/*
 * This interrupt handler will be called if the RBOX key combo is detected.
 */
static void key_combo0_irq(void)
{
	GWRITE_FIELD(RBOX, INT_STATE, INTR_BUTTON_COMBO0_RDY, 1);
	recovery_button_record();
	board_reboot_ec();
	CPRINTS("Recovery Requested");
}
DECLARE_IRQ(GC_IRQNUM_RBOX0_INTR_BUTTON_COMBO0_RDY_INT, key_combo0_irq, 0);

/**
 * Console command to toggle system (AP) reset
 */
static int command_sys_rst(int argc, char **argv)
{
	int val;
	char *e;
	int ms = 20;

	if (argc > 1) {
		if (!ccd_is_cap_enabled(CCD_CAP_REBOOT_EC_AP))
			return EC_ERROR_ACCESS_DENIED;

		if (!strcasecmp("pulse", argv[1])) {
			if (argc == 3) {
				ms = strtoi(argv[2], &e, 0);
				if (*e)
					return EC_ERROR_PARAM2;
			}
			ccprintf("Pulsing AP reset for %dms\n", ms);
			assert_sys_rst();
			msleep(ms);
			deassert_sys_rst();
		} else if (parse_bool(argv[1], &val)) {
			if (val)
				assert_sys_rst();
			else
				deassert_sys_rst();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("SYS_RST_L is %s\n", is_sys_rst_asserted() ?
		 "asserted" : "deasserted");

	return EC_SUCCESS;

}
DECLARE_SAFE_CONSOLE_COMMAND(sysrst, command_sys_rst,
	"[pulse [time] | <BOOLEAN>]",
	"Assert/deassert SYS_RST_L to reset the AP");

/*
 * Set RBOX register controlling EC reset and wait until RBOX updates the
 * output.
 *
 * Input parameter is treated as a Boolean, 1 means reset needs to be
 * asserted, 0 means reset needs to be deasserted.
 */
static void wait_ec_rst(int level)
{
	int i;


	/* Just in case. */
	level = !!level;

	GWRITE(RBOX, ASSERT_EC_RST, level);

	/*
	 * If ec_rst value is being explicitly set while power button is held
	 * pressed after reset, do not let "power button release" ISR change
	 * the ec_rst value.
	 */
	power_button_release_enable_interrupt(0);

	/*
	 * RBOX is running on its own clock, let's make sure we don't exit
	 * this function until the ecr_rst output matches the desired setting.
	 * 1000 cycles is way more than needed for RBOX to react.
	 *
	 * Note that the read back value is the inversion of the value written
	 * into the register once it propagates through RBOX.
	 */
	for (i = 0; i < 1000; i++)
		if (GREAD_FIELD(RBOX, CHECK_OUTPUT, EC_RST) != level)
			break;
}

void assert_ec_rst(void)
{
	/* Prevent bit bang interrupt storm. */
	if (uart_bitbang_is_enabled())
		task_disable_irq(bitbang_config.rx_irq);

	wait_ec_rst(1);

	/*
	 * On closed source set1, the EC requires a minimum 30 ms pulse to
	 * properly reset. Ensure EC reset is always asserted for more than
	 * this time.
	 */
	if (board_uses_closed_source_set1())
		msleep(30);
}

void deassert_ec_rst(void)
{
	wait_ec_rst(0);

	if (uart_bitbang_is_enabled())
		task_enable_irq(bitbang_config.rx_irq);
}

int is_ec_rst_asserted(void)
{
	return GREAD(RBOX, ASSERT_EC_RST);
}

/**
 * Console command to toggle EC reset
 */
static int command_ec_rst(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!ccd_is_cap_enabled(CCD_CAP_REBOOT_EC_AP))
			return EC_ERROR_ACCESS_DENIED;

		if (!strcasecmp("cl", argv[1])) {
			/* Assert EC_RST_L until TPM_RST_L is asserted */
			board_closed_loop_reset();
		} else if (!strcasecmp("pulse", argv[1])) {
			ccprintf("Pulsing EC reset\n");
			board_reboot_ec();
		} else if (parse_bool(argv[1], &val)) {
			if (val)
				assert_ec_rst();
			else
				deassert_ec_rst();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("EC_RST_L is %s\n", is_ec_rst_asserted() ?
		 "asserted" : "deasserted");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ecrst, command_ec_rst,
	"[cl | pulse | <BOOLEAN>]",
	"Assert/deassert EC_RST_L to reset the EC (and AP)");

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
	uint8_t use_i2c;
	uint8_t i2c_prop;
	uint8_t use_spi;
	uint8_t spi_prop;

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

	use_i2c = *config & 0xa0;
	use_spi = *config & 0x0a;
	/*
	 * The strap signals should have at least one pullup. Nothing can
	 * interfere with these. If we did not read any pullups, these are
	 * invalid straps. The config can't be salvaged.
	 */
	if (!use_i2c && !use_spi)
		return EC_ERROR_INVAL;
	/*
	 * The unused strap signals are used for the bus to the AP. If the AP
	 * has added pullups to the signals, it could interfere with the strap
	 * readings. If pullups are found on both the SPI and I2C straps, use
	 * the board properties to determine SPI vs I2C. We can use this to mask
	 * unused config pins the AP is interfering with.
	 */
	if (use_i2c && use_spi) {
		spi_prop = (GREG32(PMU, LONG_LIFE_SCRATCH1) &
			    BOARD_SLAVE_CONFIG_SPI);
		i2c_prop = (GREG32(PMU, LONG_LIFE_SCRATCH1) &
			    BOARD_SLAVE_CONFIG_I2C);
		/* Make sure exactly one interface is selected */
		if ((i2c_prop && spi_prop) || (!spi_prop && !i2c_prop))
			return EC_ERROR_INVAL;
		use_spi = spi_prop;
		CPRINTS("Ambiguous strap config. Use %s based on old "
			"brdprop.", use_spi ? "spi" : "i2c");
	}

	/* Now that I2C vs SPI is known, mask the unused strap bits. */
	*config &= use_spi ? 0xf : 0xf0;

	return EC_SUCCESS;
}

static uint32_t get_properties(void)
{
	int i;
	uint8_t config;
	uint32_t properties;

	if (chip_factory_mode()) {
		CPRINTS("Chip factory mode, short circuit to SPI");
		return BOARD_SLAVE_CONFIG_SPI;
	}

#ifdef H1_RED_BOARD
	CPRINTS("Unconditionally enabling SPI and platform reset");
	return (BOARD_SLAVE_CONFIG_SPI | BOARD_USE_PLT_RESET);
#endif
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
	 * get_strap_config() returned EC_SUCCESS.
	 */
	if (config & 0xa) {
		properties = BOARD_SLAVE_CONFIG_SPI;
		/*
		 * Determine PLT_RST_L vs SYS_RST_L. Any board with a pullup on
		 * DIOA9 uses PLT_RST_L.
		 */
		properties |= config & 0x8 ? BOARD_USE_PLT_RESET : 0;
	} else {
		/* All I2C boards use same default properties. */
		properties = BOARD_PROPERTIES_DEFAULT;
	}
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
						     EC_RESET_FLAG_HARD)) {
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

	/*
	 * Provide access to the SDA line to be able to detect 'hosed i2c
	 * slave' condition.
	 */
	GWRITE(PINMUX, GPIO0_GPIO14_SEL, GC_PINMUX_DIOA1_SEL);

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
static const char *key_type(const struct SignedHeader *h)
{
	if (G_SIGNED_FOR_PROD(h))
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
	uint8_t tpm_mode;

	ccprintf("Reset flags: 0x%08x (", system_get_reset_flags());
	system_print_reset_flags();
	ccprintf(")\n");
	if (system_rollback_detected())
		ccprintf("Rollback detected\n");
	ccprintf("Reset count: %d\n", reset_count);

	ccprintf("Chip:        %s %s %s\n", system_get_chip_vendor(),
		 system_get_chip_name(), system_get_chip_revision());

	active = system_get_ro_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	ccprintf("RO keyid:    0x%08x(%s)\n", h->keyid, key_type(h));

	active = system_get_image_copy();
	vaddr = get_program_memory_addr(active);
	h = (const struct SignedHeader *)vaddr;
	ccprintf("RW keyid:    0x%08x(%s)\n", h->keyid, key_type(h));

	ccprintf("DEV_ID:      0x%08x 0x%08x\n",
		 GREG32(FUSE, DEV_ID0), GREG32(FUSE, DEV_ID1));

	system_get_rollback_bits(rollback_str, sizeof(rollback_str));
	ccprintf("Rollback:    %s\n", rollback_str);

	tpm_mode = get_tpm_mode();
	ccprintf("TPM MODE:    %s (%d)\n",
		(tpm_mode == TPM_MODE_DISABLED) ? "disabled" : "enabled",
		tpm_mode);
	ccprintf("Key Ladder:  %s\n",
		DCRYPTO_ladder_is_enabled() ? "enabled" : "disabled");

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

	/* Update INFO1 mask based on the currently active image. */
	system_update_rollback_mask_with_active_img();

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
	/*
	 * The board properties are stored in LONG_LIFE_SCRATCH1.  Note that we
	 * don't just simply return board_properties here since that's just a
	 * cached value from init time.
	 */
	ccprintf("properties = 0x%x\n", GREG32(PMU, LONG_LIFE_SCRATCH1));

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(brdprop, command_board_properties,
			     NULL, "Display board properties");

int chip_factory_mode(void)
{
	static uint8_t mode_set;

	/*
	 * Bit 0x2 used to indicate that mode has been set, bit 0x1 is the
	 * actual indicator of the chip factory mode.
	 */
	if (!mode_set)
		mode_set = 2 | !!gpio_get_level(GPIO_DIOB4);

	return mode_set & 1;
}

#ifdef CR50_RELAXED
static int command_rollback(int argc, char **argv)
{
	system_ensure_rollback();
	ccprintf("Rebooting to alternate RW due to manual request\n");
	cflush();
	system_reset(0);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(rollback, command_rollback,
	"", "Force rollback to escape DEV image.");
#endif

/*
 * Set long life register bit requesting generating of the ITE SYNC sequence
 * and reboot.
 */
static void deferred_ite_sync_reset(void)
{
	/* Enable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);
	GREG32(PMU, LONG_LIFE_SCRATCH1) |= BOARD_ITE_EC_SYNC_NEEDED;
	/* Disable writing to the long life register */
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 0);

	system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED |
		     SYSTEM_RESET_HARD);
}
DECLARE_DEFERRED(deferred_ite_sync_reset);

void board_start_ite_sync(void)
{
	/* Let the usb reply to make it to the host. */
	hook_call_deferred(&deferred_ite_sync_reset_data, 10 * MSEC);
}

void board_unwedge_i2cs(void)
{
	/*
	 * Create connection between i2cs_scl and the 'unwedge_scl' GPIO, and
	 * generate the i2c stop sequence which will reset the i2cs FSM.
	 *
	 * First, disconnect the external pin from the i2cs_scl input.
	 */
	GWRITE(PINMUX, DIOA9_SEL, 0);

	/* Connect the 'unwedge' GPIO to the i2cs_scl input. */
	GWRITE(PINMUX, GPIO1_GPIO5_SEL, GC_PINMUX_I2CS0_SCL_SEL);

	/* Generate a 'stop' condition. */
	gpio_set_level(GPIO_UNWEDGE_I2CS_SCL, 1);
	usleep(2);
	GWRITE_FIELD(I2CS, CTRL_SDA_VAL, READ0_S, 1);
	usleep(2);
	GWRITE_FIELD(I2CS, CTRL_SDA_VAL, READ0_S, 0);
	usleep(2);

	/* Disconnect the 'unwedge' mode SCL. */
	GWRITE(PINMUX, GPIO1_GPIO5_SEL, 0);

	/* Restore external pin connection to the i2cs_scl. */
	GWRITE(PINMUX, DIOA9_SEL, GC_PINMUX_I2CS0_SCL_SEL);
}
