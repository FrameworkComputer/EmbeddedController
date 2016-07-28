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
#include "init_chip.h"
#include "registers.h"
#include "nvmem.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "uartn.h"
#include "usb_descriptor.h"
#include "usb_hid.h"
#include "util.h"
#include "spi.h"
#include "usb_spi.h"

/* Define interrupt and gpio structs */
#include "gpio_list.h"

#include "cryptoc/sha.h"

/*
 * TODO: NV_MEMORY_SIZE is defined in 2 places. Here and in
 * /src/third_party/tmp2/Implementation.h. This needs to be
 * fixed so that it's only defined in one location to ensure that the TPM2.0 lib
 * code and the NvMem code specific to Cr50 is consistent. Will
 * either reference existing issue or create one to track
 * this as ultimately only want this defined in 1 place.
 */
#define NV_MEMORY_SIZE 7168
#define NVMEM_TPM_SIZE NV_MEMORY_SIZE
#define NVMEM_CR50_SIZE (NVMEM_PARTITION_SIZE - NVMEM_TPM_SIZE - \
			sizeof(struct nvmem_tag))
/* NvMem user buffer lengths table */
uint32_t nvmem_user_sizes[NVMEM_NUM_USERS] = {
	NVMEM_TPM_SIZE,
	NVMEM_CR50_SIZE
};

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
	int exiten;

	delay_sleep_by(1000);

	/* Clear interrupt state */
	GWRITE_FIELD(PMU, INT_STATE, INTR_WAKEUP, 1);

	/* Clear pmu reset */
	GWRITE(PMU, CLRRST, 1);

	if (GR_PMU_EXITPD_SRC & GC_PMU_EXITPD_SRC_PIN_PD_EXIT_MASK) {
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
		delay_sleep_by(3 * SECOND);

		if (!gpio_get_level(GPIO_SYS_RST_L_IN))
			sys_rst_asserted(GPIO_SYS_RST_L_IN);
	}
}
DECLARE_IRQ(GC_IRQNUM_PMU_INTR_WAKEUP_INT, pmu_wakeup_interrupt, 1);

static void init_timers(void)
{
	/* Cancel low speed timers that may have
	 * been initialized prior to soft reset. */
	GREG32(TIMELS, TIMER0_CONTROL) = 0;
	GREG32(TIMELS, TIMER0_LOAD) = 0;
	GREG32(TIMELS, TIMER1_CONTROL) = 0;
	GREG32(TIMELS, TIMER1_LOAD) = 0;
}

static void init_interrupts(void)
{
	int i;

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

/* Initialize board. */
static void board_init(void)
{
	init_pmu();
	init_timers();
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

	if (max_regions < 2)
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

	/* Enable access to the NVRAM region */
	regions[1].reg_base = CONFIG_MAPPED_STORAGE_BASE +
		CONFIG_FLASH_NVMEM_OFFSET;
	regions[1].reg_size = CONFIG_FLASH_NVMEM_SIZE;
	regions[1].reg_perms = FLASH_REGION_EN_ALL;

	return 2;
}

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

void sys_rst_asserted(enum gpio_signal signal)
{
	/*
	 * Cr50 drives SYS_RST_L in certain scenarios, in those cases
	 * asserting this signal should not cause a system reset.
	 */
	CPRINTS("%s resceived signal %d)", __func__, signal);
	if (usb_spi_update_in_progress())
		return;

	cflush();
	system_reset(0);
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
static int servo_state_unknown(enum device_type device, int uart)
{
	if (uartn_enabled(UART_AP) && uartn_enabled(UART_EC))
		device_set_state(DEVICE_SERVO, DEVICE_STATE_UNKNOWN);

	if (uartn_enabled(uart)) {
		device_state_changed(device, DEVICE_STATE_UNKNOWN);
		return 1;
	}
	return 0;
}

static void servo_detached(enum device_type device, int uart)
{
	if (servo_state_unknown(device, uart) ||
	    device_get_state(device) == DEVICE_STATE_ON)
		return;
	device_state_changed(DEVICE_SERVO_AP, DEVICE_STATE_OFF);
	device_state_changed(DEVICE_SERVO_EC, DEVICE_STATE_OFF);

	device_set_state(DEVICE_SERVO, DEVICE_STATE_OFF);

	gpio_enable_interrupt(device_states[DEVICE_SERVO_AP].detect_on);
	gpio_enable_interrupt(device_states[DEVICE_SERVO_EC].detect_on);
}

static void device_powered_off(enum device_type device, int uart)
{
	if (device_get_state(device) == DEVICE_STATE_ON)
		return;

	device_state_changed(device, DEVICE_STATE_OFF);

	/* Disable RX and TX on the UART peripheral */
	uartn_disable(uart);

	/* Disconnect the TX pin from the UART peripheral */
	uartn_tx_disconnect(uart);

	gpio_enable_interrupt(device_states[device].detect_on);
}

static void servo_ap_deferred(void)
{
	servo_detached(DEVICE_SERVO_AP, UART_AP);
}
DECLARE_DEFERRED(servo_ap_deferred);

static void servo_ec_deferred(void)
{
	servo_detached(DEVICE_SERVO_EC, UART_EC);
}
DECLARE_DEFERRED(servo_ec_deferred);

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
	[DEVICE_SERVO_AP] = {
		.deferred = &servo_ap_deferred_data,
		.detect_on = GPIO_SERVO_UART1_ON,
		.detect_off = GPIO_SERVO_UART1_OFF,
		.name = "Servo AP"
	},
	[DEVICE_SERVO_EC] = {
		.deferred = &servo_ec_deferred_data,
		.detect_on = GPIO_SERVO_UART2_ON,
		.detect_off = GPIO_SERVO_UART2_OFF,
		.name = "Servo EC"
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
	[DEVICE_SERVO] = {
		.detect_on = GPIO_COUNT,
		.detect_off = GPIO_COUNT,
		.name = "Servo"
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

static void servo_attached(enum device_type device, int uart)
{
	if (servo_state_unknown(device, uart))
		return;

	/* Update the device state */
	device_state_changed(device, DEVICE_STATE_ON);
	device_set_state(DEVICE_SERVO, DEVICE_STATE_ON);

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
	case GPIO_SERVO_UART1_ON:
		servo_attached(DEVICE_SERVO_AP, UART_AP);
		break;
	case GPIO_SERVO_UART2_ON:
		servo_attached(DEVICE_SERVO_EC, UART_EC);
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
	case GPIO_SERVO_UART1_OFF:
		board_update_device_state(DEVICE_SERVO_AP);
		break;
	case GPIO_SERVO_UART2_OFF:
		board_update_device_state(DEVICE_SERVO_EC);
		break;
	default:
		CPRINTS("Device not supported");
	}
}

void board_update_device_state(enum device_type device)
{
	int state;

	if (device == DEVICE_SERVO)
		return;

	if (device == DEVICE_SERVO_EC || device == DEVICE_SERVO_AP) {
		/*
		 * If either AP UART TX or EC UART TX are pulled high when
		 * cr50 uart is not enabled, then servo is attached
		 */
		state = (!uartn_enabled(UART_AP) &&
			gpio_get_level(GPIO_SERVO_UART1_ON)) ||
			(!uartn_enabled(UART_EC) &&
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
