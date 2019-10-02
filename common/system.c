/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : common functions */
#include "battery.h"
#include "charge_manager.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "cros_board_info.h"
#include "dma.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "otp.h"
#include "rwsig.h"
#include "spi_flash.h"
#ifdef CONFIG_MPU
#include "mpu.h"
#endif
#include "panic.h"
#include "sysjump.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "version.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/* Round up to a multiple of 4 */
#define ROUNDUP4(x) (((x) + 3) & ~3)

/* Data for an individual jump tag */
struct jump_tag {
	uint16_t tag;		/* Tag ID */
	uint8_t data_size;	/* Size of data which follows */
	uint8_t data_version;	/* Data version */

	/* Followed by data_size bytes of data */
};

/* Jump data (at end of RAM, or preceding panic data) */
static struct jump_data *jdata;

static uint32_t reset_flags;
static int jumped_to_image;
static int disable_jump;  /* Disable ALL jumps if system is locked */
static int force_locked;  /* Force system locked even if WP isn't enabled */
static enum ec_reboot_cmd reboot_at_shutdown;

STATIC_IF(CONFIG_HIBERNATE) uint32_t hibernate_seconds;
STATIC_IF(CONFIG_HIBERNATE) uint32_t hibernate_microseconds;

/* On-going actions preventing going into deep-sleep mode */
uint32_t sleep_mask;

#ifdef CONFIG_LOW_POWER_IDLE_LIMITED
/* Set it to prevent going into idle mode */
uint32_t idle_disabled;
#endif

#ifdef CONFIG_HOSTCMD_AP_SET_SKUID
static uint32_t ap_sku_id;

uint32_t system_get_sku_id(void)
{
	return ap_sku_id;
}

#define AP_SKUID_SYSJUMP_TAG		0x4153 /* AS */
#define AP_SKUID_HOOK_VERSION		1

/**
 * Preserve AP SKUID across a sysjump.
 */

static void ap_sku_id_preserve_state(void)
{
	system_add_jump_tag(AP_SKUID_SYSJUMP_TAG, AP_SKUID_HOOK_VERSION,
			    sizeof(ap_sku_id), &ap_sku_id);
}
DECLARE_HOOK(HOOK_SYSJUMP, ap_sku_id_preserve_state, HOOK_PRIO_DEFAULT);

/**
 * Restore AP SKUID after a sysjump.
 */
static void ap_sku_id_restore_state(void)
{
	const uint32_t *prev_ap_sku_id;
	int size, version;

	prev_ap_sku_id = (const uint32_t *)system_get_jump_tag(
		AP_SKUID_SYSJUMP_TAG, &version, &size);

	if (prev_ap_sku_id && version == AP_SKUID_HOOK_VERSION &&
		size == sizeof(prev_ap_sku_id)) {
		memcpy(&ap_sku_id, prev_ap_sku_id, sizeof(ap_sku_id));
	}
}
DECLARE_HOOK(HOOK_INIT, ap_sku_id_restore_state, HOOK_PRIO_DEFAULT);
#endif

/**
 * Return the program memory address where the image `copy` begins or should
 * begin. In the case of external storage, the image may or may not currently
 * reside at the location returned.
 */
uintptr_t get_program_memory_addr(enum system_image_copy_t copy)
{
	switch (copy) {
	case SYSTEM_IMAGE_RO:
		return CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF;
	case SYSTEM_IMAGE_RW:
		return CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF;
#ifdef CHIP_HAS_RO_B
	case SYSTEM_IMAGE_RO_B:
		return CONFIG_PROGRAM_MEMORY_BASE + CHIP_RO_B_MEM_OFF;
#endif
#ifdef CONFIG_RW_B
	case SYSTEM_IMAGE_RW_B:
		return CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_B_MEM_OFF;
#endif
	default:
		return INVALID_ADDR;
	}
}

/**
 * Return the size of the image copy, or 0 if error.
 */
static uint32_t __attribute__((unused)) get_size(enum system_image_copy_t copy)
{
	/* Ensure we return aligned sizes. */
	BUILD_ASSERT(CONFIG_RO_SIZE % SPI_FLASH_MAX_WRITE_SIZE == 0);
	BUILD_ASSERT(CONFIG_RW_SIZE % SPI_FLASH_MAX_WRITE_SIZE == 0);

	switch (copy) {
	case SYSTEM_IMAGE_RO:
	case SYSTEM_IMAGE_RO_B:
		return CONFIG_RO_SIZE;
	case SYSTEM_IMAGE_RW:
	case SYSTEM_IMAGE_RW_B:
		return CONFIG_RW_SIZE;
	default:
		return 0;
	}
}

int system_is_locked(void)
{
	if (force_locked)
		return 1;

#ifdef CONFIG_SYSTEM_UNLOCKED
	/* System is explicitly unlocked */
	return 0;

#elif defined(CONFIG_FLASH)
	/*
	 * Unlocked if write protect pin deasserted or read-only firmware
	 * is not protected.
	 */
	if ((EC_FLASH_PROTECT_GPIO_ASSERTED | EC_FLASH_PROTECT_RO_NOW) &
	    ~flash_get_protect())
		return 0;

	/* If WP pin is asserted and lock is applied, we're locked */
	return 1;
#else
	/* Other configs are locked by default */
	return 1;
#endif
}

test_mockable uintptr_t system_usable_ram_end(void)
{
	/* Leave space at the end of RAM for jump data and tags.
	 *
	 * Note that jump_tag_total is 0 on a reboot, so we have the maximum
	 * amount of RAM available on a reboot; we only lose space for stored
	 * tags after a sysjump.  When verified boot runs after a reboot, it'll
	 * have as much RAM as we can give it; after verified boot jumps to
	 * another image there'll be less RAM, but we'll care less too. */
	return (uintptr_t)jdata - jdata->jump_tag_total;
}

void system_encode_save_flags(int reset_flags, uint32_t *save_flags)
{
	*save_flags = 0;

	/* Save current reset reasons if necessary */
	if (reset_flags & SYSTEM_RESET_PRESERVE_FLAGS)
		*save_flags = system_get_reset_flags() |
			      EC_RESET_FLAG_PRESERVED;

	/* Add in AP off flag into saved flags. */
	if (reset_flags & SYSTEM_RESET_LEAVE_AP_OFF)
		*save_flags |= EC_RESET_FLAG_AP_OFF;

	/* Save reset flag */
	if (reset_flags & (SYSTEM_RESET_HARD | SYSTEM_RESET_WAIT_EXT))
		*save_flags |= EC_RESET_FLAG_HARD;
	else
		*save_flags |= EC_RESET_FLAG_SOFT;
}

uint32_t system_get_reset_flags(void)
{
	return reset_flags;
}

void system_set_reset_flags(uint32_t flags)
{
	reset_flags |= flags;
}

void system_clear_reset_flags(uint32_t flags)
{
	reset_flags &= ~flags;
}

void system_print_reset_flags(void)
{
	int count = 0;
	int i;
	static const char * const reset_flag_descs[] = {
		#include "reset_flag_desc.inc"
	};

	if (!reset_flags) {
		CPUTS("unknown");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(reset_flag_descs); i++) {
		if (reset_flags & BIT(i)) {
			if (count++)
				CPUTS(" ");

			CPUTS(reset_flag_descs[i]);
		}
	}

	if (reset_flags >= BIT(i)) {
		if (count)
			CPUTS(" ");

		CPUTS("no-desc");
	}
}

int system_jumped_to_this_image(void)
{
	return jumped_to_image;
}

int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	struct jump_tag *t;

	/* Only allowed during a sysjump */
	if (!jdata || jdata->magic != JUMP_DATA_MAGIC)
		return EC_ERROR_UNKNOWN;

	/* Make room for the new tag */
	if (size > 255)
		return EC_ERROR_INVAL;
	jdata->jump_tag_total += ROUNDUP4(size) + sizeof(struct jump_tag);

	t = (struct jump_tag *)system_usable_ram_end();
	t->tag = tag;
	t->data_size = size;
	t->data_version = version;
	if (size)
		memcpy(t + 1, data, size);

	return EC_SUCCESS;
}

const uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size)
{
	const struct jump_tag *t;
	int used = 0;

	if (!jdata)
		return NULL;

	/* Search through tag data for a match */
	while (used < jdata->jump_tag_total) {
		/* Check the next tag */
		t = (const struct jump_tag *)(system_usable_ram_end() + used);
		used += sizeof(struct jump_tag) + ROUNDUP4(t->data_size);
		if (t->tag != tag)
			continue;

		/* Found a match */
		if (size)
			*size = t->data_size;
		if (version)
			*version = t->data_version;

		return (const uint8_t *)(t + 1);
	}

	/* If we're still here, no match */
	return NULL;
}

void system_disable_jump(void)
{
	disable_jump = 1;

#ifdef CONFIG_MPU
	if (system_is_locked()) {
		int ret;
		enum system_image_copy_t __attribute__((unused)) copy;

		CPRINTS("MPU type: %08x", mpu_get_type());
		/*
		 * Protect data RAM from code execution
		 */
		ret = mpu_protect_data_ram();
		if (ret == EC_SUCCESS) {
			CPRINTS("data RAM locked. Exclusion %pP-%pP",
				&__iram_text_start,
				&__iram_text_end);
		} else {
			CPRINTS("Failed to lock data RAM (%d)", ret);
			return;
		}

#if defined(CONFIG_EXTERNAL_STORAGE) || !defined(CONFIG_FLASH_PHYSICAL)
		/*
		 * Protect code RAM from being overwritten
		 */
		ret = mpu_protect_code_ram();
		if (ret == EC_SUCCESS) {
			CPRINTS("code RAM locked.");
		} else {
			CPRINTS("Failed to lock code RAM (%d)", ret);
			return;
		}
#else
		/*
		 * Protect inactive image (ie. RO if running RW, vice versa)
		 * from code execution.
		 */
		switch (system_get_image_copy()) {
		case SYSTEM_IMAGE_RO:
			ret =  mpu_lock_rw_flash();
			copy = SYSTEM_IMAGE_RW;
			break;
		case SYSTEM_IMAGE_RW:
			ret =  mpu_lock_ro_flash();
			copy = SYSTEM_IMAGE_RO;
			break;
		default:
			copy = SYSTEM_IMAGE_UNKNOWN;
			ret = !EC_SUCCESS;
		}
		if (ret == EC_SUCCESS) {
			CPRINTS("%s image locked",
				system_image_copy_t_to_string(copy));
		} else {
			CPRINTS("Failed to lock %s image (%d)",
				system_image_copy_t_to_string(copy), ret);
			return;
		}
#endif /* !CONFIG_EXTERNAL_STORAGE */

		/* All regions were configured successfully, enable MPU */
		mpu_enable();
	} else {
		CPRINTS("System is unlocked. Skip MPU configuration");
	}
#endif /* CONFIG_MPU */
}

test_mockable enum system_image_copy_t system_get_image_copy(void)
{
#ifdef CONFIG_EXTERNAL_STORAGE
	/* Return which region is used in program memory */
	return system_get_shrspi_image_copy();
#else
	uintptr_t my_addr = (uintptr_t)system_get_image_copy -
			    CONFIG_PROGRAM_MEMORY_BASE;

	if (my_addr >= CONFIG_RO_MEM_OFF &&
	    my_addr < (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE))
		return SYSTEM_IMAGE_RO;

	if (my_addr >= CONFIG_RW_MEM_OFF &&
	    my_addr < (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE))
		return SYSTEM_IMAGE_RW;

#ifdef CHIP_HAS_RO_B
	if (my_addr >= CHIP_RO_B_MEM_OFF &&
	    my_addr < (CHIP_RO_B_MEM_OFF + CONFIG_RO_SIZE))
		return SYSTEM_IMAGE_RO_B;
#endif

#ifdef CONFIG_RW_B
	if (my_addr >= CONFIG_RW_B_MEM_OFF &&
	    my_addr < (CONFIG_RW_B_MEM_OFF + CONFIG_RW_SIZE))
		return SYSTEM_IMAGE_RW_B;
#endif

	return SYSTEM_IMAGE_UNKNOWN;
#endif
}

test_mockable int system_unsafe_to_overwrite(uint32_t offset, uint32_t size)
{
	uint32_t r_offset;
	uint32_t r_size;
	enum system_image_copy_t copy = system_get_image_copy();

	switch (copy) {
	case SYSTEM_IMAGE_RO:
		r_size = CONFIG_RO_SIZE;
		break;
	case SYSTEM_IMAGE_RW:
	case SYSTEM_IMAGE_RW_B:
		r_size = CONFIG_RW_SIZE;
#ifdef CONFIG_RWSIG
		/* Allow RW sig to be overwritten */
		r_size -= CONFIG_RW_SIG_SIZE;
#endif
		break;
	default:
		return 0;
	}
	r_offset = flash_get_rw_offset(copy);

	if ((offset >= r_offset && offset < (r_offset + r_size)) ||
	    (r_offset >= offset && r_offset < (offset + size)))
		return 1;
	else
		return 0;
}

const char *system_get_image_copy_string(void)
{
	return system_image_copy_t_to_string(system_get_image_copy());
}

const char *system_image_copy_t_to_string(enum system_image_copy_t copy)
{
	static const char * const image_names[] = {
		"unknown", "RO", "RW", "RO_B", "RW_B"
	};
	return image_names[copy < ARRAY_SIZE(image_names) ? copy : 0];
}

/**
 * Jump to what we hope is the init address of an image.
 *
 * This function does not return.
 *
 * @param init_addr	Init address of target image
 */
static void jump_to_image(uintptr_t init_addr)
{
	void (*resetvec)(void);

	/*
	 * Jumping to any image asserts the signal to the Silego chip that that
	 * EC is not in read-only firmware.  (This is not technically true if
	 * jumping from RO -> RO, but that's not a meaningful use case...).
	 *
	 * Pulse the signal long enough to set the latch in the Silego, then
	 * drop it again so we don't leak power through the pulldown in the
	 * Silego.
	 */
	gpio_set_level(GPIO_ENTERING_RW, 1);
	usleep(MSEC);
	gpio_set_level(GPIO_ENTERING_RW, 0);

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	/* Note: must be before i2c module is locked down */
	pd_prepare_sysjump();
#endif

#ifdef CONFIG_I2C_MASTER
	/* Prepare I2C module for sysjump */
	i2c_prepare_sysjump();
#endif

	/* Flush UART output */
	cflush();

	/* Fill in preserved data between jumps */
	jdata->reserved0 = 0;
	jdata->magic = JUMP_DATA_MAGIC;
	jdata->version = JUMP_DATA_VERSION;
	jdata->reset_flags = reset_flags;
	jdata->jump_tag_total = 0;  /* Reset tags */
	jdata->struct_size = sizeof(struct jump_data);

	/* Call other hooks; these may add tags */
	hook_notify(HOOK_SYSJUMP);

	/* Disable interrupts before jump */
	interrupt_disable();

#ifdef CONFIG_DMA
	/* Disable all DMA channels to avoid memory corruption */
	dma_disable_all();
#endif /* CONFIG_DMA */

	/* Jump to the reset vector */
	resetvec = (void(*)(void))init_addr;
	resetvec();
}

static int is_rw_image(enum system_image_copy_t copy)
{
	return copy == SYSTEM_IMAGE_RW || copy == SYSTEM_IMAGE_RW_B;
}

int system_is_in_rw(void)
{
	return is_rw_image(system_get_image_copy());
}

test_mockable int system_run_image_copy(enum system_image_copy_t copy)
{
	uintptr_t base;
	uintptr_t init_addr;

	/* If system is already running the requested image, done */
	if (system_get_image_copy() == copy)
		return EC_SUCCESS;

	if (system_is_locked()) {
		/* System is locked, so disallow jumping between images unless
		 * this is the initial jump from RO to RW code. */

		/* Must currently be running the RO image */
		if (system_get_image_copy() != SYSTEM_IMAGE_RO)
			return EC_ERROR_ACCESS_DENIED;

		/* Target image must be RW image */
		if (!is_rw_image(copy))
			return EC_ERROR_ACCESS_DENIED;

		/* Jumping must still be enabled */
		if (disable_jump)
			return EC_ERROR_ACCESS_DENIED;
	}

	/* Load the appropriate reset vector */
	base = get_program_memory_addr(copy);
	if (base == 0xffffffff)
		return EC_ERROR_INVAL;

	if (IS_ENABLED(CONFIG_EXTERNAL_STORAGE)) {
		/* Jump to loader */
		init_addr = system_get_lfw_address();
		system_set_image_copy(copy);
	} else if (IS_ENABLED(CONFIG_FW_RESET_VECTOR)) {
		/* Get reset vector */
		init_addr = system_get_fw_reset_vector(base);
	} else {
		uintptr_t init = base + 4;

		/* Skip any head room in the RO image */
		if (copy == SYSTEM_IMAGE_RO)
			init += CONFIG_RO_HEAD_ROOM;

		init_addr = *(uintptr_t *)(init);

		/* Make sure the reset vector is inside the destination image */
		if (!IS_ENABLED(EMU_BUILD) &&
		    (init_addr < base || init_addr >= base + get_size(copy)))
			return EC_ERROR_UNKNOWN;
	}

	CPRINTS("Jumping to image %s", system_image_copy_t_to_string(copy));

	jump_to_image(init_addr);

	/* Should never get here */
	return EC_ERROR_UNKNOWN;
}

enum system_image_copy_t system_get_active_copy(void)
{
	uint8_t slot;
	if (system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, &slot))
		slot = SYSTEM_IMAGE_RW_A;
	/* This makes it return RW_A by default. For example, this happens when
	 * BBRAM isn't initialized. */
	return slot == SYSTEM_IMAGE_RW_B ? slot : SYSTEM_IMAGE_RW_A;
}

enum system_image_copy_t system_get_update_copy(void)
{
#ifdef CONFIG_VBOOT_EFS
	return system_get_active_copy() == SYSTEM_IMAGE_RW_A ?
			SYSTEM_IMAGE_RW_B : SYSTEM_IMAGE_RW_A;
#else
	return SYSTEM_IMAGE_RW_A;
#endif
}

int system_set_active_copy(enum system_image_copy_t copy)
{
	return system_set_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, copy);
}

/*
 * This is defined in system.c instead of flash.c because it's called even
 * on the boards which don't include flash.o. (e.g. hadoken, stm32l476g-eval)
 */
uint32_t flash_get_rw_offset(enum system_image_copy_t copy)
{
#ifdef CONFIG_VBOOT_EFS
	if (copy == SYSTEM_IMAGE_RW_B)
		return CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_B_STORAGE_OFF;
#endif
	if (is_rw_image(copy))
		return CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;

	return CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_RO_STORAGE_OFF;
}

const struct image_data *system_get_image_data(enum system_image_copy_t copy)
{
	static struct image_data data;

	uintptr_t addr;
	enum system_image_copy_t active_copy = system_get_image_copy();

	/* Handle version of current image */
	if (copy == active_copy || copy == SYSTEM_IMAGE_UNKNOWN)
		return &current_image_data;
	if (active_copy == SYSTEM_IMAGE_UNKNOWN)
		return NULL;

	/*
	 * The version string is always located after the reset vectors, so
	 * it's the same offset as in the current image.  Find that offset.
	 */
	addr = ((uintptr_t)&current_image_data -
	       get_program_memory_addr(active_copy));

	/*
	 * Read the version information from the proper location
	 * on storage.
	 */
	addr += flash_get_rw_offset(copy);

#ifdef CONFIG_MAPPED_STORAGE
	addr += CONFIG_MAPPED_STORAGE_BASE;
	flash_lock_mapped_storage(1);
	memcpy(&data, (const void *)addr, sizeof(data));
	flash_lock_mapped_storage(0);
#else
	/* Read the version struct from flash into a buffer. */
	if (flash_read(addr, sizeof(data), (char *)&data))
		return NULL;
#endif

	/* Make sure the version struct cookies match before returning the
	 * version string. */
	if (data.cookie1 == current_image_data.cookie1 &&
	    data.cookie2 == current_image_data.cookie2)
		return &data;

	return NULL;
}

__attribute__((weak))	   /* Weird chips may need their own implementations */
const char *system_get_version(enum system_image_copy_t copy)
{
	const struct image_data *data = system_get_image_data(copy);

	return data ? data->version : "";
}

#ifdef CONFIG_ROLLBACK
int32_t system_get_rollback_version(enum system_image_copy_t copy)
{
	const struct image_data *data = system_get_image_data(copy);

	return data ? data->rollback_version : -1;
}
#endif

int system_get_image_used(enum system_image_copy_t copy)
{
	const struct image_data *data = system_get_image_data(copy);

	return data ? MAX((int)data->size, 0) : 0;
}

/*
 * Returns positive board version if successfully retrieved. Otherwise the
 * value is a negative version of an EC return code. Without this optimization
 * multiple boards run out of flash size.
 */
int system_get_board_version(void)
{
#if defined(CONFIG_BOARD_VERSION_CUSTOM)
	return board_get_version();
#elif defined(CONFIG_BOARD_VERSION_GPIO)
	return
		(!!gpio_get_level(GPIO_BOARD_VERSION1) << 0) |
		(!!gpio_get_level(GPIO_BOARD_VERSION2) << 1) |
		(!!gpio_get_level(GPIO_BOARD_VERSION3) << 2);
#elif defined(CONFIG_BOARD_VERSION_CBI)
	int error;
	int32_t version;

	error = cbi_get_board_version(&version);
	if (error)
		return -error;
	else
		return version;
#else
	return 0;
#endif
}

__attribute__((weak))	   /* Weird chips may need their own implementations */
const char *system_get_build_info(void)
{
	return build_info;
}

void system_common_pre_init(void)
{
	uintptr_t addr;

#ifdef CONFIG_SOFTWARE_PANIC
	/*
	 * Log panic cause if watchdog caused reset and panic cause
	 * was not already logged. This must happen before calculating
	 * jump_data address because it might change panic pointer.
	 */
	if (system_get_reset_flags() & EC_RESET_FLAG_WATCHDOG) {
		uint32_t reason;
		uint32_t info;
		uint8_t exception;

		panic_get_reason(&reason, &info, &exception);
		if (reason != PANIC_SW_WATCHDOG)
			panic_set_reason(PANIC_SW_WATCHDOG, 0, 0);
	}
#endif

	/*
	 * Put the jump data before the panic data, or at the end of RAM if
	 * panic data is not present.
	 */
	addr = (uintptr_t)panic_get_data();
	if (!addr)
		addr = CONFIG_RAM_BASE + CONFIG_RAM_SIZE;

	jdata = (struct jump_data *)(addr - sizeof(struct jump_data));

	/*
	 * Check jump data if this is a jump between images.  Jumps all show up
	 * as an unknown reset reason, because we jumped directly from one
	 * image to another without actually triggering a chip reset.
	 */
	if (jdata->magic == JUMP_DATA_MAGIC &&
	    jdata->version >= 1 &&
	    reset_flags == 0) {
		/* Change in jump data struct size between the previous image
		 * and this one. */
		int delta;

		/* Yes, we jumped to this image */
		jumped_to_image = 1;
		/* Restore the reset flags */
		reset_flags = jdata->reset_flags | EC_RESET_FLAG_SYSJUMP;

		/*
		 * If the jump data structure isn't the same size as the
		 * current one, shift the jump tags to immediately before the
		 * current jump data structure, to make room for initalizing
		 * the new fields below.
		 */
		if (jdata->version == 1)
			delta = 0;  /* No tags in v1, so no need for move */
		else if (jdata->version == 2)
			delta = sizeof(struct jump_data) - JUMP_DATA_SIZE_V2;
		else
			delta = sizeof(struct jump_data) - jdata->struct_size;

		if (delta && jdata->jump_tag_total) {
			uint8_t *d = (uint8_t *)system_usable_ram_end();
			memmove(d, d + delta, jdata->jump_tag_total);
		}

		/* Initialize fields added after version 1 */
		if (jdata->version < 2)
			jdata->jump_tag_total = 0;

		/* Initialize fields added after version 2 */
		if (jdata->version < 3)
			jdata->reserved0 = 0;

		/* Struct size is now the current struct size */
		jdata->struct_size = sizeof(struct jump_data);

		/*
		 * Clear the jump struct's magic number.  This prevents
		 * accidentally detecting a jump when there wasn't one, and
		 * disallows use of system_add_jump_tag().
		 */
		jdata->magic = 0;
	} else {
		/* Clear the whole jump_data struct */
		memset(jdata, 0, sizeof(struct jump_data));
	}
}

/**
 * Handle a pending reboot command.
 */
static int handle_pending_reboot(enum ec_reboot_cmd cmd)
{
	switch (cmd) {
	case EC_REBOOT_CANCEL:
		return EC_SUCCESS;
	case EC_REBOOT_JUMP_RO:
		return system_run_image_copy(SYSTEM_IMAGE_RO);
	case EC_REBOOT_JUMP_RW:
		return system_run_image_copy(system_get_active_copy());
	case EC_REBOOT_COLD:
#ifdef HAS_TASK_PDCMD
		/*
		 * Reboot the PD chip(s) as well, but first suspend the ports
		 * if this board has PD tasks running so they don't query the
		 * TCPCs while they reset.
		 */
#ifdef HAS_TASK_PD_C0
		{
			int port;

			for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT;
			     port++)
				pd_set_suspend(port, 1);
		}
#endif
		board_reset_pd_mcu();
#endif

		cflush();
		system_reset(SYSTEM_RESET_HARD);
		/* That shouldn't return... */
		return EC_ERROR_UNKNOWN;
	case EC_REBOOT_DISABLE_JUMP:
		system_disable_jump();
		return EC_SUCCESS;
	case EC_REBOOT_HIBERNATE_CLEAR_AP_OFF:
		if (!IS_ENABLED(CONFIG_HIBERNATE))
			return EC_ERROR_INVAL;

		if (IS_ENABLED(CONFIG_POWER_BUTTON_INIT_IDLE)) {
			CPRINTS("Clearing AP_OFF");
			chip_save_reset_flags(chip_read_reset_flags() &
					      ~EC_RESET_FLAG_AP_OFF);
		}
		/* Intentional fall-through */
	case EC_REBOOT_HIBERNATE:
		if (!IS_ENABLED(CONFIG_HIBERNATE))
			return EC_ERROR_INVAL;

		CPRINTS("system hibernating");
		system_hibernate(hibernate_seconds, hibernate_microseconds);
		/* That shouldn't return... */
		return EC_ERROR_UNKNOWN;
	default:
		return EC_ERROR_INVAL;
	}
}

/*****************************************************************************/
/* Hooks */

static void system_common_shutdown(void)
{
	if (reboot_at_shutdown)
		CPRINTF("Reboot at shutdown: %d\n", reboot_at_shutdown);
	handle_pending_reboot(reboot_at_shutdown);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, system_common_shutdown, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_SYSINFO
static int command_sysinfo(int argc, char **argv)
{
	ccprintf("Reset flags: 0x%08x (", system_get_reset_flags());
	system_print_reset_flags();
	ccprintf(")\n");
	ccprintf("Copy:   %s\n", system_get_image_copy_string());
	ccprintf("Jumped: %s\n", system_jumped_to_this_image() ? "yes" : "no");

	ccputs("Flags: ");
	if (system_is_locked()) {
		ccputs(" locked");
		if (force_locked)
			ccputs(" (forced)");
		if (disable_jump)
			ccputs(" jump-disabled");
	} else
		ccputs(" unlocked");
	ccputs("\n");

	if (reboot_at_shutdown)
		ccprintf("Reboot at shutdown: %d\n", reboot_at_shutdown);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(sysinfo, command_sysinfo,
			     NULL,
			     "Print system info");
#endif

#ifdef CONFIG_CMD_SCRATCHPAD
static int command_scratchpad(int argc, char **argv)
{
	int rv = EC_SUCCESS;

	if (argc == 2) {
		char *e;
		int s = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		rv = system_set_scratchpad(s);
	}

	ccprintf("Scratchpad: 0x%08x\n", system_get_scratchpad());
	return rv;
}
DECLARE_CONSOLE_COMMAND(scratchpad, command_scratchpad,
			"[val]",
			"Get or set scratchpad value");
#endif /* CONFIG_CMD_SCRATCHPAD */

__maybe_unused static int command_hibernate(int argc, char **argv)
{
	int seconds = 0;
	int microseconds = 0;

	if (argc >= 2)
		seconds = strtoi(argv[1], NULL, 0);
	if (argc >= 3)
		microseconds = strtoi(argv[2], NULL, 0);

	if (seconds || microseconds)
		ccprintf("Hibernating for %d.%06d s\n", seconds, microseconds);
	else
		ccprintf("Hibernating until wake pin asserted.\n");

	/*
	 * If chipset is already off, then call system_hibernate directly. Else,
	 * let chipset_task bring down the power rails and transition to proper
	 * state before system_hibernate is called.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		system_hibernate(seconds, microseconds);
	else {
		reboot_at_shutdown = EC_REBOOT_HIBERNATE;
		hibernate_seconds = seconds;
		hibernate_microseconds = microseconds;

		chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	}

	return EC_SUCCESS;
}
#ifdef CONFIG_HIBERNATE
DECLARE_CONSOLE_COMMAND(hibernate, command_hibernate,
			"[sec] [usec]",
			"Hibernate the EC");
#endif /* CONFIG_HIBERNATE */

/*
 * A typical build string has the following format
 *
 * <version> <build_date_time> <user@buildhost>
 *
 * some EC board, however, are composed of multiple components, their build
 * strings can include several subcomponent versions between the main version
 * and the build date, for instance
 *
 * cr50_v1.1.4979-0061603+ private-cr51:v0.0.66-bd9a0fe tpm2:v0.0.259-2b...
 *
 * Each subcomponent in this case includes the ":v" substring. For these
 * combined version strings this function prints each version or subcomponent
 * version on a different line.
 */
static void print_build_string(void)
{
	const char *full_build_string;
	const char *p;
	char symbol;
	int seen_colonv;

	ccprintf("Build:   ");
	full_build_string = system_get_build_info();

	/* 50 characters or less, will fit into the terminal line. */
	if (strlen(full_build_string) < 50) {
		ccprintf("%s\n", full_build_string);
		return;
	}

	/*
	 * Build version string needs splitting, let's split it at the first
	 * space (this is where the main version ends), and then on each space
	 * after the ":v" substring, this is where subcomponent versions are
	 * separated.
	 */
	p = full_build_string;
	seen_colonv = 1;

	symbol = *p++;
	while (symbol) {
		if ((symbol == ' ') && seen_colonv) {
			seen_colonv = 0;
			/* Indent each line under 'Build:    ' */
			ccprintf("\n         ");
		} else {
			if ((symbol == ':') && (*p == 'v'))
				seen_colonv = 1;
			ccprintf("%c", symbol);
		}
		symbol = *p++;
	}
	ccprintf("\n");
}

static int command_version(int argc, char **argv)
{
	int board_version;

	ccprintf("Chip:    %s %s %s\n", system_get_chip_vendor(),
		 system_get_chip_name(), system_get_chip_revision());

	board_version = system_get_board_version();
	if (board_version < 0)
		ccprintf("Board:   Error %d\n", -board_version);
	else
		ccprintf("Board:   %d\n", board_version);

#ifdef CHIP_HAS_RO_B
	{
		enum system_image_copy_t active;

		active = system_get_ro_image_copy();
		ccprintf("RO_A:  %c %s\n",
			 (active == SYSTEM_IMAGE_RO ? '*' : ' '),
			 system_get_version(SYSTEM_IMAGE_RO));
		ccprintf("RO_B:  %c %s\n",
			 (active == SYSTEM_IMAGE_RO_B ? '*' : ' '),
			 system_get_version(SYSTEM_IMAGE_RO_B));
	}
#else
	ccprintf("RO:      %s\n", system_get_version(SYSTEM_IMAGE_RO));
#endif
#ifdef CONFIG_RW_B
	{
		enum system_image_copy_t active;

		active = system_get_image_copy();
		ccprintf("RW_A:  %c %s\n",
			 (active == SYSTEM_IMAGE_RW ? '*' : ' '),
			 system_get_version(SYSTEM_IMAGE_RW));
		ccprintf("RW_B:  %c %s\n",
			 (active == SYSTEM_IMAGE_RW_B ? '*' : ' '),
			 system_get_version(SYSTEM_IMAGE_RW_B));
	}
#else
	ccprintf("RW:      %s\n", system_get_version(SYSTEM_IMAGE_RW));
#endif

	system_print_extended_version_info();
	print_build_string();

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(version, command_version,
			     NULL,
			     "Print versions");

#ifdef CONFIG_CMD_SYSJUMP
static int command_sysjump(int argc, char **argv)
{
	uint32_t addr;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Handle named images */
	if (!strcasecmp(argv[1], "RO"))
		return system_run_image_copy(SYSTEM_IMAGE_RO);
	else if (!strcasecmp(argv[1], "RW") || !strcasecmp(argv[1], "A"))
		return system_run_image_copy(SYSTEM_IMAGE_RW);
	else if (!strcasecmp(argv[1], "B")) {
#ifdef CONFIG_RW_B
		return system_run_image_copy(SYSTEM_IMAGE_RW_B);
#else
		return EC_ERROR_PARAM1;
#endif
	} else if (!strcasecmp(argv[1], "disable")) {
		system_disable_jump();
		return EC_SUCCESS;
	}

	/* Arbitrary jumps are only allowed on an unlocked system */
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	/* Check for arbitrary address */
	addr = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	ccprintf("Jumping to 0x%08x\n", addr);
	cflush();
	jump_to_image(addr);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysjump, command_sysjump,
			"[RO | RW | A | B | addr | disable]",
			"Jump to a system image or address");
#endif

static int command_reboot(int argc, char **argv)
{
	int flags = SYSTEM_RESET_MANUALLY_TRIGGERED;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcasecmp(argv[i], "hard") ||
		    !strcasecmp(argv[i], "cold")) {
			flags |= SYSTEM_RESET_HARD;
		} else if (!strcasecmp(argv[i], "soft")) {
			flags &= ~SYSTEM_RESET_HARD;
		} else if (!strcasecmp(argv[i], "ap-off")) {
			flags |= SYSTEM_RESET_LEAVE_AP_OFF;
		} else if (!strcasecmp(argv[i], "cancel")) {
			reboot_at_shutdown = EC_REBOOT_CANCEL;
			return EC_SUCCESS;
		} else if (!strcasecmp(argv[i], "preserve")) {
			flags |= SYSTEM_RESET_PRESERVE_FLAGS;
		} else if (!strcasecmp(argv[i], "wait-ext")) {
			flags |= SYSTEM_RESET_WAIT_EXT;
		} else
			return EC_ERROR_PARAM1 + i - 1;
	}

	if (flags & SYSTEM_RESET_HARD)
		ccputs("Hard-");
	if (flags & SYSTEM_RESET_WAIT_EXT)
		ccputs("Waiting for ext reset!\n\n\n");
	else
		ccputs("Rebooting!\n\n\n");
	cflush();

	system_reset(flags);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(reboot, command_reboot,
			"[hard|soft] [preserve] [ap-off] [wait-ext] [cancel]",
			"Reboot the EC");

#ifdef CONFIG_CMD_SYSLOCK
static int command_system_lock(int argc, char **argv)
{
	force_locked = 1;
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(syslock, command_system_lock,
			     NULL,
			     "Lock the system, even if WP is disabled");
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_CMD_SLEEPMASK)
/**
 * Modify and print the sleep mask which controls access to deep sleep
 * mode in the idle task.
 */
static int command_sleepmask(int argc, char **argv)
{
#ifdef CONFIG_CMD_SLEEPMASK_SET
	int v;

	if (argc >= 2) {
		if (parse_bool(argv[1], &v)) {
			if (v)
				disable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
			else
				enable_sleep(SLEEP_MASK_FORCE_NO_DSLEEP);
		} else {
			char *e;
			v = strtoi(argv[1], &e, 10);
			if (*e)
				return EC_ERROR_PARAM1;

			/* Set sleep mask directly. */
			sleep_mask = v;
		}
	}
#endif
	ccprintf("sleep mask: %08x\n", sleep_mask);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(sleepmask, command_sleepmask,
			     "[ on | off | <sleep_mask>]",
			     "Display/force sleep mask");
#endif

#ifdef CONFIG_CMD_JUMPTAGS
static int command_jumptags(int argc, char **argv)
{
	const struct jump_tag *t;
	int used = 0;

	/* Jump tags valid only after a sysjump */
	if (!jdata)
		return EC_SUCCESS;

	while (used < jdata->jump_tag_total) {
		/* Check the next tag */
		t = (const struct jump_tag *)(system_usable_ram_end() + used);
		used += sizeof(struct jump_tag) + ROUNDUP4(t->data_size);

		ccprintf("%08x: 0x%04x %c%c.%d %3d\n",
			 (uintptr_t)t,
			 t->tag, t->tag >> 8, (uint8_t)t->tag,
			 t->data_version, t->data_size);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(jumptags, command_jumptags,
			NULL,
			"List jump tags");
#endif /* CONFIG_CMD_JUMPTAGS */

#ifdef CONFIG_EMULATED_SYSRQ
static int command_sysrq(int argc, char **argv)
{
	char key = 'x';

	if (argc > 1 && argv[1])
		key = argv[1][0];

	host_send_sysrq(key);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysrq, command_sysrq,
			"[key]",
			"Simulate sysrq press (default: x)");
#endif /* CONFIG_EMULATED_SYSRQ */

/*****************************************************************************/
/* Host commands */

static enum ec_status
host_command_get_version(struct host_cmd_handler_args *args)
{
	struct ec_response_get_version *r = args->response;
	enum system_image_copy_t active_slot = system_get_active_copy();

	strzcpy(r->version_string_ro, system_get_version(SYSTEM_IMAGE_RO),
		sizeof(r->version_string_ro));
	strzcpy(r->version_string_rw,
		system_get_version(active_slot),
		sizeof(r->version_string_rw));

	switch (system_get_image_copy()) {
	case SYSTEM_IMAGE_RO:
		r->current_image = EC_IMAGE_RO;
		break;
	case SYSTEM_IMAGE_RW:
	case SYSTEM_IMAGE_RW_B:
		r->current_image = EC_IMAGE_RW;
		break;
	default:
		r->current_image = EC_IMAGE_UNKNOWN;
		break;
	}

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_VERSION,
		     host_command_get_version,
		     EC_VER_MASK(0));

#ifdef CONFIG_HOSTCMD_SKUID
static enum ec_status
host_command_get_sku_id(struct host_cmd_handler_args *args)
{
	struct ec_sku_id_info *r = args->response;

	r->sku_id = system_get_sku_id();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_SKU_ID,
		     host_command_get_sku_id,
		     EC_VER_MASK(0));
#endif

#ifdef CONFIG_HOSTCMD_AP_SET_SKUID
static enum ec_status
host_command_set_sku_id(struct host_cmd_handler_args *args)
{
	const struct ec_sku_id_info *p = args->params;

	ap_sku_id = p->sku_id;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SET_SKU_ID,
		     host_command_set_sku_id,
		     EC_VER_MASK(0));
#endif

#ifdef CONFIG_KEYBOARD_LANGUAGE_ID
static enum ec_status
host_command_get_keyboard_id(struct host_cmd_handler_args *args)
{
	struct ec_response_keyboard_id *r = args->response;

	r->keyboard_id = keyboard_get_keyboard_id();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_KEYBOARD_ID,
		     host_command_get_keyboard_id,
		     EC_VER_MASK(0));
#endif

static enum ec_status
host_command_build_info(struct host_cmd_handler_args *args)
{
	strzcpy(args->response, system_get_build_info(), args->response_max);
	args->response_size = strlen(args->response) + 1;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_BUILD_INFO,
		     host_command_build_info,
		     EC_VER_MASK(0));

static enum ec_status
host_command_get_chip_info(struct host_cmd_handler_args *args)
{
	struct ec_response_get_chip_info *r = args->response;

	strzcpy(r->vendor, system_get_chip_vendor(), sizeof(r->vendor));
	strzcpy(r->name, system_get_chip_name(), sizeof(r->name));
	strzcpy(r->revision, system_get_chip_revision(), sizeof(r->revision));

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CHIP_INFO,
		     host_command_get_chip_info,
		     EC_VER_MASK(0));

#ifdef CONFIG_BOARD_VERSION
enum ec_status
host_command_get_board_version(struct host_cmd_handler_args *args)
{
	struct ec_response_board_version *r = args->response;
	int board_version;

	board_version = system_get_board_version();
	if (board_version < 0) {
		CPRINTS("Failed (%d) getting board version", -board_version);
		return EC_RES_ERROR;
	}

	r->board_version = board_version;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_BOARD_VERSION,
		     host_command_get_board_version,
		     EC_VER_MASK(0));
#endif

#ifdef CONFIG_HOSTCMD_VBNV_CONTEXT
enum ec_status host_command_vbnvcontext(struct host_cmd_handler_args *args)
{
	const struct ec_params_vbnvcontext *p = args->params;
	struct ec_response_vbnvcontext *r;
	int i;

	switch (p->op) {
	case EC_VBNV_CONTEXT_OP_READ:
		r = args->response;
		for (i = 0; i < EC_VBNV_BLOCK_SIZE; ++i)
			if (system_get_bbram(SYSTEM_BBRAM_IDX_VBNVBLOCK0 + i,
					     r->block + i))
				return EC_RES_ERROR;
		args->response_size = sizeof(*r);
		break;
	case EC_VBNV_CONTEXT_OP_WRITE:
		for (i = 0; i < EC_VBNV_BLOCK_SIZE; ++i)
			if (system_set_bbram(SYSTEM_BBRAM_IDX_VBNVBLOCK0 + i,
					     p->block[i]))
				return EC_RES_ERROR;
		break;
	default:
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_VBNV_CONTEXT,
		     host_command_vbnvcontext,
		     EC_VER_MASK(EC_VER_VBNV_CONTEXT));
#endif /* CONFIG_HOSTCMD_VBNV_CONTEXT */

enum ec_status host_command_reboot(struct host_cmd_handler_args *args)
{
	struct ec_params_reboot_ec p;

	/*
	 * Ensure reboot parameters don't get clobbered when the response
	 * is sent in case data argument points to the host tx/rx buffer.
	 */
	memcpy(&p, args->params, sizeof(p));

	if (p.cmd == EC_REBOOT_CANCEL) {
		/* Cancel pending reboot */
		reboot_at_shutdown = EC_REBOOT_CANCEL;
		return EC_RES_SUCCESS;
	}

	if (p.flags & EC_REBOOT_FLAG_SWITCH_RW_SLOT) {
#ifdef CONFIG_VBOOT_EFS
		if (system_set_active_copy(system_get_update_copy()))
			CPRINTS("Failed to set active slot");
#else
		return EC_RES_INVALID_PARAM;
#endif
	}
	if (p.flags & EC_REBOOT_FLAG_ON_AP_SHUTDOWN) {
		/* Store request for processing at chipset shutdown */
		reboot_at_shutdown = p.cmd;
		return EC_RES_SUCCESS;
	}

#ifdef HAS_TASK_HOSTCMD
	if (p.cmd == EC_REBOOT_JUMP_RO ||
	    p.cmd == EC_REBOOT_JUMP_RW ||
	    p.cmd == EC_REBOOT_COLD ||
	    p.cmd == EC_REBOOT_HIBERNATE) {
		/* Clean busy bits on host for commands that won't return */
		args->result = EC_RES_SUCCESS;
		host_send_response(args);
	}
#endif

	CPRINTS("Executing host reboot command %d", p.cmd);
	switch (handle_pending_reboot(p.cmd)) {
	case EC_SUCCESS:
		return EC_RES_SUCCESS;
	case EC_ERROR_INVAL:
		return EC_RES_INVALID_PARAM;
	case EC_ERROR_ACCESS_DENIED:
		return EC_RES_ACCESS_DENIED;
	default:
		return EC_RES_ERROR;
	}
}
DECLARE_HOST_COMMAND(EC_CMD_REBOOT_EC,
		     host_command_reboot,
		     EC_VER_MASK(0));

int system_can_boot_ap(void)
{
	int soc = -1;
	int pow = -1;

#if defined(CONFIG_BATTERY) && \
	defined(CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
	/* Require a minimum battery level to power on. If battery isn't
	 * present, battery_state_of_charge_abs returns false. */
	if (battery_state_of_charge_abs(&soc) == EC_SUCCESS &&
			soc >= CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
		return 1;
#endif

#if defined(CONFIG_CHARGE_MANAGER) && \
	defined(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON)
	pow = charge_manager_get_power_limit_uw() / 1000;
	if (pow >= CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON)
		return 1;
#else
	/* For fixed AC system */
	return 1;
#endif

	CPRINTS("Not enough power to boot (%d %%, %d mW)", soc, pow);
	return 0;
}

#ifdef CONFIG_SERIALNO_LEN
/* By default, read serial number from flash, can be overridden. */
__overridable const char *board_read_serial(void)
{
	if (IS_ENABLED(CONFIG_FLASH_PSTATE) &&
	    IS_ENABLED(CONFIG_FLASH_PSTATE_BANK))
		return flash_read_pstate_serial();
	else if (IS_ENABLED(CONFIG_OTP))
		return otp_read_serial();
	else
		return "";
}

__overridable int board_write_serial(const char *serialno)
{
	if (IS_ENABLED(CONFIG_FLASH_PSTATE) &&
	    IS_ENABLED(CONFIG_FLASH_PSTATE_BANK))
		return flash_write_pstate_serial(serialno);
	else if (IS_ENABLED(CONFIG_OTP))
		return otp_write_serial(serialno);
	else
		return EC_ERROR_UNIMPLEMENTED;
}
#endif  /* CONFIG_SERIALNO_LEN */


__attribute__((weak))
void clock_enable_module(enum module_id module, int enable)
{
	/*
	 * Default weak implementation - for chips that don't support this
	 * function.
	 */
}

__test_only void system_common_reset_state(void)
{
	jdata = 0;
	reset_flags = 0;
	jumped_to_image = 0;
}
