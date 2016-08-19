/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "dcrypto/dcrypto.h"
#include "device_state.h"
#include "ec_version.h"
#include "flash_config.h"
#include "gpio.h"
#include "hooks.h"
#include "i2cs.h"
#include "init_chip.h"
#include "nvmem.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "tpm_registers.h"
#include "trng.h"
#include "uartn.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "usb_spi.h"
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
		 * If sys_rst_l is configured to wake on low and the signal is
		 * low then call sys_rst_asserted
		 */
		if (!gpio_get_level(GPIO_SYS_RST_L_IN) &&
		    GREAD_FIELD(PINMUX, EXITINV0, DIOM0))
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

enum permission_level {
	PERMISSION_LOW = 0x00,
	PERMISSION_MEDIUM = 0x33,    /* APPS run at medium */
	PERMISSION_HIGH = 0x3C,
	PERMISSION_HIGHEST = 0x55
};

/* Drop run level to at least medium. */
static void init_runlevel(const enum permission_level desired_level)
{
	volatile uint32_t *const reg_addrs[] = {
		/* CPU's use of the system peripheral bus */
		GREG32_ADDR(GLOBALSEC, CPU0_S_PERMISSION),
		/* CPU's use of the system bus via the debug access port */
		GREG32_ADDR(GLOBALSEC, CPU0_S_DAP_PERMISSION),
		/* DMA's use of the system peripheral bus */
		GREG32_ADDR(GLOBALSEC, DDMA0_PERMISSION),
		/* Current software level affects which (if any) scratch
		 * registers can be used for a warm boot hardware-verified
		 * jump. */
		GREG32_ADDR(GLOBALSEC, SOFTWARE_LVL),
	};
	int i;

	/* Permission registers drop by 1 level (e.g. HIGHEST -> HIGH)
	 * each time a write is encountered (the value written does
	 * not matter).  So we repeat writes and reads, until the
	 * desired level is reached.
	 */
	for (i = 0; i < ARRAY_SIZE(reg_addrs); i++) {
		uint32_t current_level;

		while (1) {
			current_level = *reg_addrs[i];
			if (current_level <= desired_level)
				break;
			*reg_addrs[i] = desired_level;
		}
	}
}

static void configure_board_specific_gpios(void)
{
	/* Add a pullup to sys_rst_l */
	if (system_get_board_properties() & BOARD_NEEDS_SYS_RST_PULL_UP)
		GWRITE_FIELD(PINMUX, DIOM0_CTL, PU, 1);
}

/* Initialize board. */
static void board_init(void)
{
	configure_board_specific_gpios();
	init_pmu();
	init_interrupts();
	init_trng();
	init_jittery_clock(1);
	init_runlevel(PERMISSION_MEDIUM);
	/* Initialize NvMem partitions */
	nvmem_init();

	/* TODO(crosbug.com/p/49959): For now, leave flash WP unlocked */
	GREG32(RBOX, EC_WP_L) = 1;

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
	[USB_STR_HID_NAME] = USB_STRING_DESC("PokeyPokey"),
	[USB_STR_AP_NAME] = USB_STRING_DESC("AP"),
	[USB_STR_EC_NAME] = USB_STRING_DESC("EC"),
	[USB_STR_UPGRADE_NAME] = USB_STRING_DESC("Firmware upgrade"),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("AP EC upgrade"),
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
	CPRINTS("%s", __func__);
	if (usb_spi_update_in_progress() || is_sys_rst_asserted())
		return;

	cflush();
	system_reset(0);
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

static void device_state_changed(enum device_type device,
				 enum device_state state)
{
	device_set_state(device, state);

	/* Disable interrupts */
	gpio_disable_interrupt(device_states[device].detect_on);
	gpio_disable_interrupt(device_states[device].detect_off);

	/*
	 * We've determined the device state, so cancel any deferred callbacks.
	 */
	hook_call_deferred(device_states[device].deferred, -1);
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

static void device_powered_off(enum device_type device, int uart)
{
	if (device_get_state(device) == DEVICE_STATE_ON)
		return;

	device_state_changed(device, DEVICE_STATE_OFF);

	if (uart) {
		/* Disable RX and TX on the UART peripheral */
		uartn_disable(uart);

		/* Disconnect the TX pin from the UART peripheral */
		uartn_tx_disconnect(uart);
	}

	gpio_enable_interrupt(device_states[device].detect_on);
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
	device_powered_off(DEVICE_AP, UART_AP);
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
		.detect_on = GPIO_SERVO_UART2_ON,
		.detect_off = GPIO_SERVO_UART2_OFF,
		.name = "Servo"
	},
	[DEVICE_AP] = {
		.deferred = &ap_deferred_data,
		.detect_on = GPIO_AP_ON,
		.detect_off = GPIO_AP_OFF,
		.name = "AP"
	},
	[DEVICE_EC] = {
		.deferred = &ec_deferred_data,
		.detect_on = GPIO_EC_ON,
		.detect_off = GPIO_EC_OFF,
		.name = "EC"
	},
};
BUILD_ASSERT(ARRAY_SIZE(device_states) == DEVICE_COUNT);

static void device_powered_on(enum device_type device, int uart)
{
	/* Update the device state */
	device_state_changed(device, DEVICE_STATE_ON);

	/* Enable RX and TX on the UART peripheral */
	uartn_enable(uart);

	/* Connect the TX pin to the UART TX Signal */
	if (device_get_state(DEVICE_SERVO) != DEVICE_STATE_ON &&
	    !uartn_enabled(uart))
		uartn_tx_connect(uart);
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
}

void device_state_on(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_AP_ON:
		device_powered_on(DEVICE_AP, UART_AP);
		break;
	case GPIO_EC_ON:
		device_powered_on(DEVICE_EC, UART_EC);
		break;
	case GPIO_SERVO_UART2_ON:
		servo_attached();
		break;
	default:
		CPRINTS("Device not supported");
		return;
	}
}

void device_state_off(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_AP_OFF:
		board_update_device_state(DEVICE_AP);
		break;
	case GPIO_EC_OFF:
		board_update_device_state(DEVICE_EC);
		break;
	case GPIO_SERVO_UART2_OFF:
		board_update_device_state(DEVICE_SERVO);
		break;
	default:
		CPRINTS("Device not supported");
	}
}

void board_update_device_state(enum device_type device)
{
	int state;

	if (device == DEVICE_SERVO) {
		/*
		 * If EC UART TX is pulled high when EC UART is not enabled,
		 * then servo is attached.
		 */
		state = (!uartn_enabled(UART_EC) &&
			gpio_get_level(GPIO_SERVO_UART2_ON));
	} else
		state = gpio_get_level(device_states[device].detect_on);

	/*
	 * If the device is currently on set its state immediately. If it
	 * thinks the device is powered off debounce the signal.
	 */
	if (state)
		device_state_on(device_states[device].detect_on);
	else {
		device_set_state(device, DEVICE_STATE_UNKNOWN);

		gpio_enable_interrupt(device_states[device].detect_on);
		/*
		 * Wait a bit. If cr50 detects this device is ever powered on
		 * during this time then the status wont be set to powered off.
		 */
		hook_call_deferred(device_states[device].deferred, 50);
	}
}

void system_init_board_properties(void)
{
	uint32_t properties;

	properties = GREG32(PMU, LONG_LIFE_SCRATCH1);

	/*
	 * This must be a power on reset or maybe restart due to a software
	 * update from a version not setting the register.
	 */
	if (!properties || system_get_reset_flags() & RESET_FLAG_HARD) {
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
			 * TODO(crosbug.com/p/56540): enable UART0 RX on Reef.
			 * Early reef boards dont have the necessary pullups on
			 * UART0RX so disable it until that is fixed.
			 */
			properties |= BOARD_DISABLE_UART0_RX;
			/*
			 * Use receiving a usb set address request as a
			 * benchmark for marking the updated image as good.
			 */
			properties |= BOARD_MARK_UPDATE_ON_USB_REQ;
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

uint32_t system_board_properties_callback(void)
{
	return board_properties;
}

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
	 * Enable pull ups on both signals. TODO(vbendeb): consider
	 * adjusting pull strength.
	 */
	GWRITE_FIELD(PINMUX, DIOA1_CTL, PU, 1);
	GWRITE_FIELD(PINMUX, DIOA9_CTL, PU, 1);
	/* TODO(scollyer): Do we need to add wake on SCL activity here? */

}
