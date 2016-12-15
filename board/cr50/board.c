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
#include "rdd.h"
#include "registers.h"
#include "scratch_reg1.h"
#include "signed_header.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "tpm_registers.h"
#include "trng.h"
#include "uartn.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_spi.h"
#include "usb_i2c.h"
#include "util.h"

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

#define NVMEM_CR50_SIZE 300
#define NVMEM_TPM_SIZE ((sizeof((struct nvmem_partition *)0)->buffer) \
			- NVMEM_CR50_SIZE)

/*
 * Make sure NV memory size definition in Implementation.h matches reality. It
 * should be set to
 *
 * NVMEM_PARTITION_SIZE - NVMEM_CR50_SIZE - 8
 */
BUILD_ASSERT(NVMEM_TPM_SIZE == NV_MEMORY_SIZE);

/* NvMem user buffer lengths table */
uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
	NVMEM_TPM_SIZE,
	NVMEM_CR50_SIZE
};

/*  Board specific configuration settings */
static uint32_t board_properties;
static uint8_t reboot_request_posted;

int board_has_ap_usb(void)
{
	return !!(board_properties & BOARD_USB_AP);
}

int board_has_plt_rst(void)
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

/* I2C Port definition */
const struct i2c_port_t i2c_ports[]  = {
	{"master", I2C_PORT_MASTER, 100,
	 GPIO_I2C_SCL_INA, GPIO_I2C_SDA_INA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void post_reboot_request(void)
{
	/* Reboot the device next time TPM reset is requested. */
	reboot_request_posted = 1;
}

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
	int plt_rst_asserted;

	delay_sleep_by(1 * MSEC);

	wakeup_src = GR_PMU_EXITPD_SRC;

	/* Clear interrupt state */
	GWRITE_FIELD(PMU, INT_STATE, INTR_WAKEUP, 1);

	/* Clear pmu reset */
	GWRITE(PMU, CLRRST, 1);

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
		delay_sleep_by(3 * MINUTE);

		/*
		 * If sys_rst_l or plt_rst_l (if signal is present) is
		 * configured to wake on low and the signal is low, then call
		 * sys_rst_asserted
		 */
		plt_rst_asserted = board_properties & BOARD_USE_PLT_RESET ?
			!gpio_get_level(GPIO_PLT_RST_L) : 0;
		if ((!gpio_get_level(GPIO_SYS_RST_L_IN) &&
		     GREAD_FIELD(PINMUX, EXITINV0, DIOM0)) || (plt_rst_asserted
		     && GREAD_FIELD(PINMUX, EXITINV0, DIOM3)))
			sys_rst_asserted(GPIO_SYS_RST_L_IN);
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
	/* TODO remove i2cs wake event */

	/* Remove the pulldown on EC uart tx and disable the input */
	GWRITE_FIELD(PINMUX, DIOB5_CTL, PD, 0);
	GWRITE_FIELD(PINMUX, DIOB5_CTL, IE, 0);

	/*
	 * DIOA3 is GPIO_DETECT_AP which is used to detect if the AP is in S0.
	 * If the AP is in s0, cr50 should not be in deep sleep so wake up.
	 */
	GWRITE_FIELD(PINMUX, EXITEDGE0, DIOA3, 1); /* edge sensitive */
	GWRITE_FIELD(PINMUX, EXITINV0, DIOA3, 0);  /* wake on high */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOA3, 1);   /* GPIO_DETECT_AP */

	/*
	 * Whether it is a short pulse or long one waking on the rising edge is
	 * fine because the goal of sys_rst is to reset the TPM and after
	 * resuming from deep sleep the TPM will be reset. Cr50 doesn't need to
	 * read the low value and then reset.
	 *
	 * Configure cr50 to resume on the rising edge of sys_rst_l
	 */
	/* Disable sys_rst_l as a wake pin */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 0);
	/* Reconfigure and reenable it. */
	GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM0, 1); /* edge sensitive */
	GWRITE_FIELD(PINMUX, EXITINV0, DIOM0, 0);  /* wake on high */
	GWRITE_FIELD(PINMUX, EXITEN0, DIOM0, 1);   /* enable powerdown exit */

	/*
	 * If the board includes plt_rst_l, configure Cr50 to resume on the
	 * rising edge of this signal.
	 */
	if (board_has_plt_rst()) {
		/* Disable sys_rst_l as a wake pin */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 0);
		/* Reconfigure and reenable it. */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM3, 1); /* edge sensitive */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM3, 0);  /* wake on high */
		/* enable powerdown exit */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 1);
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

static void configure_board_specific_gpios(void)
{
	/* Add a pullup to sys_rst_l */
	if (board_rst_pullup_needed())
		GWRITE_FIELD(PINMUX, DIOM0_CTL, PU, 1);

	/* Connect PLT_RST_L signal to the pinmux */
	if (board_has_plt_rst()) {
		/* Signal using GPIO1 pin 10 for DIOA13 */
		GWRITE(PINMUX, GPIO1_GPIO10_SEL, GC_PINMUX_DIOM3_SEL);
		/* Enbale the input */
		GWRITE_FIELD(PINMUX, DIOM3_CTL, IE, 1);

		/* Set power down for the equivalent of DIO_WAKE_FALLING */
		/* Set to be edge sensitive */
		GWRITE_FIELD(PINMUX, EXITEDGE0, DIOM3, 1);
		/* Select failling edge polarity */
		GWRITE_FIELD(PINMUX, EXITINV0, DIOM3, 1);
		/* Enable powerdown exit on DIOM3 */
		GWRITE_FIELD(PINMUX, EXITEN0, DIOM3, 1);
	}
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

	/* Indication that firmware is running, for debug purposes. */
	GREG32(PMU, PWRDN_SCRATCH16) = 0xCAFECAFE;
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
		CONFIG_FLASH_NVMEM_OFFSET_A;
	regions[1].reg_size = NVMEM_PARTITION_SIZE;
	regions[1].reg_perms = FLASH_REGION_EN_ALL;

	/* Enable access to the NVRAM partition B region */
	regions[2].reg_base = CONFIG_MAPPED_STORAGE_BASE +
		CONFIG_FLASH_NVMEM_OFFSET_B;
	regions[2].reg_size = NVMEM_PARTITION_SIZE;
	regions[2].reg_perms = FLASH_REGION_EN_ALL;

	return 3;
}

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* This is the interrupt handler to react to SYS_RST_L_IN */
void sys_rst_asserted(enum gpio_signal signal)
{
	/*
	 * Cr50 drives SYS_RST_L in certain scenarios, in those cases
	 * this signal's assertion should be ignored here.
	 */
	CPRINTS("%s from %d", __func__, signal);
	if (usb_spi_update_in_progress() ||
	    tpm_is_resetting()) {
		CPRINTS("%s ignored", __func__);
		return;
	}

	if (reboot_request_posted)
		system_reset(SYSTEM_RESET_HARD);  /* This will never return. */

	/* Re-initialize the TPM software state */
	tpm_reset(0, 0);
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

void nvmem_compute_sha(uint8_t *p_buf, int num_bytes,
		       uint8_t *p_sha, int sha_len)
{
	uint8_t sha1_digest[SHA_DIGEST_SIZE];
	/*
	 * Taking advantage of the built in dcrypto engine to generate
	 * a CRC-like value that can be used to validate contents of an
	 * NvMem partition. Only using the lower 4 bytes of the sha1 hash.
	 */
	DCRYPTO_SHA1_hash((uint8_t *)p_buf,
			  num_bytes,
			  sha1_digest);
	memcpy(p_sha, sha1_digest, sha_len);
}

static int device_state_changed(enum device_type device,
				 enum device_state state)
{
	int state_changed = state != device_states[device].last_known_state;

	device_set_state(device, state);

	/*
	 * We've determined the device state, so cancel any deferred callbacks.
	 */
	hook_call_deferred(device_states[device].deferred, -1);

	return state_changed;
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

static int device_powered_off(enum device_type device, int uart)
{
	if (device_get_state(device) == DEVICE_STATE_ON)
		return EC_ERROR_UNKNOWN;

	if (!device_state_changed(device, DEVICE_STATE_OFF))
		return EC_ERROR_UNKNOWN;

	if (uart) {
		/* Disable RX and TX on the UART peripheral */
		uartn_disable(uart);

		/* Disconnect the TX pin from the UART peripheral */
		uartn_tx_disconnect(uart);
	}
	return EC_SUCCESS;
}

static void servo_deferred(void)
{
	if (servo_state_unknown())
		return;

	device_powered_off(DEVICE_SERVO, 0);
}
DECLARE_DEFERRED(servo_deferred);

static void ap_deferred(void)
{
	if (device_powered_off(DEVICE_AP, UART_AP) == EC_SUCCESS)
		hook_notify(HOOK_CHIPSET_SHUTDOWN);
}
DECLARE_DEFERRED(ap_deferred);

static void ec_deferred(void)
{
	device_powered_off(DEVICE_EC, UART_EC);
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
		.detect = GPIO_DETECT_AP,
		.name = "AP"
	},
	[DEVICE_EC] = {
		.deferred = &ec_deferred_data,
		.detect = GPIO_DETECT_EC,
		.name = "EC"
	},
};
BUILD_ASSERT(ARRAY_SIZE(device_states) == DEVICE_COUNT);

/* Returns EC_SUCCESS if the device state changed to on */
static int device_powered_on(enum device_type device, int uart)
{
	/* Update the device state */
	if (!device_state_changed(device, DEVICE_STATE_ON))
		return EC_ERROR_UNKNOWN;

	/* Enable RX and TX on the UART peripheral */
	uartn_enable(uart);

	/* Connect the TX pin to the UART TX Signal */
	if (device_get_state(DEVICE_SERVO) != DEVICE_STATE_ON &&
	    !uartn_enabled(uart))
		uartn_tx_connect(uart);

	return EC_SUCCESS;
}

static void servo_attached(void)
{
	if (servo_state_unknown())
		return;

	/* Update the device state */
	device_state_changed(DEVICE_SERVO, DEVICE_STATE_ON);

	/* Disconnect AP and EC UART when servo is attached */
	uartn_tx_disconnect(UART_AP);
	uartn_tx_disconnect(UART_EC);

	/* Disconnect i2cm interface to ina */
	usb_i2c_board_disable(0);
}

void device_state_on(enum gpio_signal signal)
{
	gpio_disable_interrupt(signal);

	switch (signal) {
	case GPIO_DETECT_AP:
		if (device_powered_on(DEVICE_AP, UART_AP) == EC_SUCCESS)
			hook_notify(HOOK_CHIPSET_RESUME);
		break;
	case GPIO_DETECT_EC:
		device_powered_on(DEVICE_EC, UART_EC);
		break;
	case GPIO_DETECT_SERVO:
		servo_attached();
		break;
	default:
		CPRINTS("Device not supported");
		return;
	}
}

void board_update_device_state(enum device_type device)
{
	if (device == DEVICE_SERVO && servo_state_unknown())
		return;

	/*
	 * If the device is currently on set its state immediately. If it
	 * thinks the device is powered off debounce the signal.
	 */
	if (gpio_get_level(device_states[device].detect))
		device_state_on(device_states[device].detect);
	else {
		device_set_state(device, DEVICE_STATE_UNKNOWN);

		gpio_enable_interrupt(device_states[device].detect);

		/*
		 * The signal is low now, but the detect signals are on UART RX
		 * which may be receiving something. Wait long enough for an
		 * entire data chunk to be sent to declare that the device is
		 * off. If the detect signal remains low for 100us then the
		 * signal is low because the device is off.
		 */
		hook_call_deferred(device_states[device].deferred, 100);
	}
}

void disable_int_ap_l(void)
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
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, disable_int_ap_l, HOOK_PRIO_DEFAULT);

void enable_int_ap_l(void)
{
	/*
	 * AP is powering up, set the I2C host sync signal to output and set
	 * it high which is the default level.
	 */
	gpio_set_flags(GPIO_INT_AP_L, GPIO_OUT_HIGH);
	gpio_set_level(GPIO_INT_AP_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_int_ap_l, HOOK_PRIO_DEFAULT);

static void init_board_properties(void)
{
	uint32_t properties;

	properties = GREG32(PMU, LONG_LIFE_SCRATCH1);

	/*
	 * This must be a power on reset or maybe restart due to a software
	 * update from a version not setting the register.
	 */
	if (!properties || (system_get_reset_flags() & RESET_FLAG_HARD)) {
		/*
		 * Reset the properties, because after a hard reset the register
		 * won't be cleared.
		 */
		properties = 0;

		/* Read DIOA1 strap pin */
		if (gpio_get_level(GPIO_STRAP0)) {
			/* Strap is pulled high -> Kevin SPI TPM option */
			properties |= BOARD_SLAVE_CONFIG_SPI;
			/* Add an internal pull up on sys_rst_l */
			/*
			 * TODO(crosbug.com/p/56945): Remove once SYS_RST_L can
			 * be pulled up externally.
			 */
			properties |= BOARD_NEEDS_SYS_RST_PULL_UP;
		} else {
			/* Strap is low -> Reef I2C TPM option */
			properties |= BOARD_SLAVE_CONFIG_I2C;
			/* One PHY is connected to the AP */
			properties |= BOARD_USB_AP;
			/*
			 * Platform reset is present and will need to be
			 * configured as a an falling edge interrupt.
			 */
			properties |= BOARD_USE_PLT_RESET;
		}

		/*
		 * Now save the properties value for future use.
		 *
		 * First enable write access to the LONG_LIFE_SCRATCH1 register.
		 */
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG1, 1);
		/* Save properties in LONG_LIFE register */
		GREG32(PMU, LONG_LIFE_SCRATCH1) = properties;
		/* Disabel write access to the LONG_LIFE_SCRATCH1 register */
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

	ccprintf("Reset flags: 0x%08x (", system_get_reset_flags());
	system_print_reset_flags();
	ccprintf(")\n");

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
	struct SignedHeader *header;
	uint32_t ctrl;
	uint32_t base_addr;
	uint32_t size;
	const char zero[4] = {}; /* value to write to magic. */

	if (system_get_image_copy() == SYSTEM_IMAGE_RW) {
		header = (struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW_B);
	} else {
		header = (struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW);
	}

	/* save the original flash region6 register values */
	ctrl = GREAD(GLOBALSEC, FLASH_REGION6_CTRL);
	base_addr = GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR);
	size = GREG32(GLOBALSEC, FLASH_REGION6_SIZE);

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

	*response_size = 0;

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_INVALIDATE_INACTIVE_RW,
	vc_invalidate_inactive_rw);
