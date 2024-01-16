/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC : common functions */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "battery.h"
#include "charge_manager.h"
#include "chipset.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "cros_board_info.h"
#include "dma.h"
#include "extpower.h"
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
#include "cros_version.h"
#include "panic.h"
#include "sysjump.h"
#include "system.h"
#include "system_boot_time.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "watchdog.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Round up to a multiple of 4 */
#define ROUNDUP4(x) (((x) + 3) & ~3)

/* Data for an individual jump tag */
struct jump_tag {
	uint16_t tag; /* Tag ID */
	uint8_t data_size; /* Size of data which follows */
	uint8_t data_version; /* Data version */

	/* Followed by data_size bytes of data */
};

/* Jump data (at end of RAM, or preceding panic data) */
static struct jump_data *jdata;

static uint32_t reset_flags; /* EC_RESET_FLAG_* */
static int jumped_to_image;
static int disable_jump; /* Disable ALL jumps if system is locked */
static int force_locked; /* Force system locked even if WP isn't enabled */
static struct ec_params_reboot_ec reboot_at_shutdown;

static enum sysinfo_flags system_info_flags;

/* Ensure enough space for panic_data, jump_data and at least one jump tag */
BUILD_ASSERT((sizeof(struct panic_data) + sizeof(struct jump_data) +
	      JUMP_TAG_MAX_SIZE) <= CONFIG_PRESERVED_END_OF_RAM_SIZE,
	     "End of ram data size is too small for panic and jump data");

STATIC_IF(CONFIG_HIBERNATE) uint32_t hibernate_seconds;
STATIC_IF(CONFIG_HIBERNATE) uint32_t hibernate_microseconds;

/* On-going actions preventing going into deep-sleep mode */
atomic_t sleep_mask;

#ifdef CONFIG_LOW_POWER_IDLE_LIMITED
/* Set it to prevent going into idle mode */
atomic_t idle_disabled;
#endif

/* SKU ID sourced from AP */
static uint32_t ap_sku_id;

#ifdef CONFIG_HOSTCMD_AP_SET_SKUID

#define AP_SKUID_SYSJUMP_TAG 0x4153 /* AS */
#define AP_SKUID_HOOK_VERSION 1

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

__overridable uint32_t board_get_sku_id(void)
{
	return 0;
}

uint32_t system_get_sku_id(void)
{
	if (IS_ENABLED(CONFIG_HOSTCMD_AP_SET_SKUID))
		return ap_sku_id;

	return board_get_sku_id();
}

/**
 * Return the program memory address where the image `copy` begins or should
 * begin. In the case of external storage, the image may or may not currently
 * reside at the location returned.
 */
uintptr_t get_program_memory_addr(enum ec_image copy)
{
	switch (copy) {
	case EC_IMAGE_RO:
		return CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF;
	case EC_IMAGE_RW:
		return CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_MEM_OFF;
#ifdef CHIP_HAS_RO_B
	case EC_IMAGE_RO_B:
		return CONFIG_PROGRAM_MEMORY_BASE + CHIP_RO_B_MEM_OFF;
#endif
#ifdef CONFIG_RW_B
	case EC_IMAGE_RW_B:
		return CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RW_B_MEM_OFF;
#endif
	default:
		return INVALID_ADDR;
	}
}

/**
 * Return the size of the image copy, or 0 if error.
 */
static uint32_t __attribute__((unused)) get_size(enum ec_image copy)
{
	/* Ensure we return aligned sizes. */
	BUILD_ASSERT(CONFIG_RO_SIZE % SPI_FLASH_MAX_WRITE_SIZE == 0);
	BUILD_ASSERT(CONFIG_RW_SIZE % SPI_FLASH_MAX_WRITE_SIZE == 0);

	switch (copy) {
	case EC_IMAGE_RO:
	case EC_IMAGE_RO_B:
		return CONFIG_RO_SIZE;
	case EC_IMAGE_RW:
	case EC_IMAGE_RW_B:
		return CONFIG_RW_SIZE;
	default:
		return 0;
	}
}

test_mockable int system_is_locked(void)
{
	static int is_locked = -1;

	if (force_locked)
		return 1;
	if (is_locked != -1)
		return is_locked;

#ifdef CONFIG_SYSTEM_UNLOCKED
	/* System is explicitly unlocked */
	is_locked = 0;
	return 0;

#elif defined(CONFIG_FLASH_CROS)
	/*
	 * Unlocked if write protect pin deasserted or read-only firmware
	 * is not protected.
	 */
	if ((EC_FLASH_PROTECT_GPIO_ASSERTED | EC_FLASH_PROTECT_RO_NOW) &
	    ~crec_flash_get_protect()) {
		is_locked = 0;
		return 0;
	}

	/* If WP pin is asserted and lock is applied, we're locked */
	is_locked = 1;
	return 1;
#else
	/* Other configs are locked by default */
	is_locked = 1;
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

void system_encode_save_flags(int flags, uint32_t *save_flags)
{
	*save_flags = 0;

	/* Save current reset reasons if necessary */
	if (flags & SYSTEM_RESET_PRESERVE_FLAGS)
		*save_flags = system_get_reset_flags() |
			      EC_RESET_FLAG_PRESERVED;

	/* Add in AP off flag into saved flags. */
	if (flags & SYSTEM_RESET_LEAVE_AP_OFF)
		*save_flags |= EC_RESET_FLAG_AP_OFF;

	/* Add in stay in RO flag into saved flags. */
	if (flags & SYSTEM_RESET_STAY_IN_RO)
		*save_flags |= EC_RESET_FLAG_STAY_IN_RO;

	/* Add in watchdog flag into saved flags. */
	if (flags & SYSTEM_RESET_AP_WATCHDOG)
		*save_flags |= EC_RESET_FLAG_AP_WATCHDOG;

	/* Save reset flag */
	if (flags & (SYSTEM_RESET_HARD | SYSTEM_RESET_WAIT_EXT))
		*save_flags |= EC_RESET_FLAG_HARD;
	else if (flags & SYSTEM_RESET_HIBERNATE)
		*save_flags |= EC_RESET_FLAG_HIBERNATE;
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

static void print_reset_flags(uint32_t flags)
{
	int count = 0;
	int i;
	static const char *const reset_flag_descs[] = {
#include "reset_flag_desc.inc"
	};

	if (!flags) {
		CPUTS("unknown");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(reset_flag_descs); i++) {
		if (flags & BIT(i)) {
			if (count++)
				CPUTS(" ");

			CPRINTF("%s", reset_flag_descs[i]);
		}
	}

	if (flags >= BIT(i)) {
		if (count)
			CPUTS(" ");

		CPUTS("no-desc");
	}
}

void system_print_reset_flags(void)
{
	print_reset_flags(reset_flags);
}

void system_print_banner(void)
{
	/* be less verbose if we boot for USB resume to meet spec timings */
	if (!(system_get_reset_flags() & EC_RESET_FLAG_USB_RESUME)) {
		CPUTS("\n");
		if (system_jumped_to_this_image())
			CPRINTS("UART initialized after sysjump");
		else
			CPUTS("\n--- UART initialized after reboot ---\n");
		CPRINTF("[Image: %s, %s]\n", system_get_image_copy_string(),
			system_get_build_info());
		CPUTS("[Reset cause: ");
		system_print_reset_flags();
		CPUTS("]\n");
	}
}

#ifdef CONFIG_RAM_SIZE
struct jump_data *get_jump_data(void)
{
	uintptr_t addr;

	/*
	 * Put the jump data before the panic data, or at the end of RAM if
	 * panic data is not present.
	 */
	addr = get_panic_data_start();
	if (!addr)
		addr = CONFIG_RAM_BASE + CONFIG_RAM_SIZE;

	return (struct jump_data *)(addr - sizeof(struct jump_data));
}
#endif

test_mockable int system_jumped_to_this_image(void)
{
	return jumped_to_image;
}

test_mockable int system_jumped_late(void)
{
	return !(reset_flags & EC_RESET_FLAG_EFS) && jumped_to_image;
}

int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	struct jump_tag *t;
	size_t new_entry_size;

	/* Only allowed during a sysjump */
	if (!jdata || jdata->magic != JUMP_DATA_MAGIC)
		return EC_ERROR_UNKNOWN;

	/* Make room for the new tag */
	if (size > JUMP_TAG_MAX_SIZE)
		return EC_ERROR_INVAL;

	new_entry_size = ROUNDUP4(size) + sizeof(struct jump_tag);

	if (system_usable_ram_end() - new_entry_size < JUMP_DATA_MIN_ADDRESS) {
		ccprintf("ERROR: out of space for jump tags\n");
		return EC_ERROR_INVAL;
	}

	jdata->jump_tag_total += new_entry_size;

	t = (struct jump_tag *)system_usable_ram_end();
	t->tag = tag;
	t->data_size = size;
	t->data_version = version;
	if (size)
		memcpy(t + 1, data, size);

	return EC_SUCCESS;
}

test_mockable const uint8_t *system_get_jump_tag(uint16_t tag, int *version,
						 int *size)
{
	const struct jump_tag *t;
	int used = 0;

	if (!jdata)
		return NULL;

	/* Ensure system_usable_ram_end() is within bounds */
	if (system_usable_ram_end() < JUMP_DATA_MIN_ADDRESS)
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

test_mockable void system_disable_jump(void)
{
	disable_jump = 1;

#ifdef CONFIG_MPU
	if (system_is_locked()) {
#ifndef CONFIG_ZEPHYR
		int ret;
		enum ec_image __attribute__((unused)) copy;

		CPRINTS("MPU type: %08x", mpu_get_type());
		/*
		 * Protect data RAM from code execution
		 */
		ret = mpu_protect_data_ram();
		if (ret == EC_SUCCESS) {
			CPRINTS("data RAM locked. Exclusion %p-%p",
				&__iram_text_start, &__iram_text_end);
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
		case EC_IMAGE_RO:
			ret = mpu_lock_rw_flash();
			copy = EC_IMAGE_RW;
			break;
		case EC_IMAGE_RW:
			ret = mpu_lock_ro_flash();
			copy = EC_IMAGE_RO;
			break;
		default:
			copy = EC_IMAGE_UNKNOWN;
			ret = !EC_SUCCESS;
		}
		if (ret == EC_SUCCESS) {
			CPRINTS("%s image locked", ec_image_to_string(copy));
		} else {
			CPRINTS("Failed to lock %s image (%d)",
				ec_image_to_string(copy), ret);
			return;
		}
#endif /* !CONFIG_EXTERNAL_STORAGE */
#endif /* !CONFIG_ZEPHYR */

		/* All regions were configured successfully, enable MPU */
		mpu_enable();
	} else {
		CPRINTS("System is unlocked. Skip MPU configuration");
	}
#endif /* CONFIG_MPU */
}

test_mockable enum ec_image system_get_image_copy(void)
{
#ifdef CONFIG_EXTERNAL_STORAGE
	/* Return which region is used in program memory */
	return system_get_shrspi_image_copy();
#else
	uintptr_t my_addr =
		(uintptr_t)system_get_image_copy - CONFIG_PROGRAM_MEMORY_BASE;

	if (my_addr >= CONFIG_RO_MEM_OFF &&
	    my_addr < (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE))
		return EC_IMAGE_RO;

	if (my_addr >= CONFIG_RW_MEM_OFF &&
	    my_addr < (CONFIG_RW_MEM_OFF + CONFIG_RW_SIZE))
		return EC_IMAGE_RW;

#ifdef CHIP_HAS_RO_B
	if (my_addr >= CHIP_RO_B_MEM_OFF &&
	    my_addr < (CHIP_RO_B_MEM_OFF + CONFIG_RO_SIZE))
		return EC_IMAGE_RO_B;
#endif

#ifdef CONFIG_RW_B
	if (my_addr >= CONFIG_RW_B_MEM_OFF &&
	    my_addr < (CONFIG_RW_B_MEM_OFF + CONFIG_RW_SIZE))
		return EC_IMAGE_RW_B;
#endif

	return EC_IMAGE_UNKNOWN;
#endif
}

test_mockable int system_unsafe_to_overwrite(uint32_t offset, uint32_t size)
{
	uint32_t r_offset;
	uint32_t r_size;
	enum ec_image copy = system_get_image_copy();

	switch (copy) {
	case EC_IMAGE_RO:
		r_size = CONFIG_RO_SIZE;
		break;
	case EC_IMAGE_RW:
	case EC_IMAGE_RW_B:
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
	return ec_image_to_string(system_get_image_copy());
}

const char *ec_image_to_string(enum ec_image copy)
{
	static const char *const image_names[] = { "unknown", "RO", "RW",
						   "RO_B", "RW_B" };
	return image_names[copy < ARRAY_SIZE(image_names) ? copy : 0];
}

__overridable void board_pulse_entering_rw(void)
{
	gpio_set_level(GPIO_ENTERING_RW, 1);
	usleep(MSEC);
	gpio_set_level(GPIO_ENTERING_RW, 0);
}

/**
 * Jump to what we hope is the init address of an image.
 *
 * This function does not return.
 *
 * @param init_addr	Init address of target image
 */
test_mockable_static void jump_to_image(uintptr_t init_addr)
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
	board_pulse_entering_rw();

	/*
	 * Since in EFS2, USB/PD won't be enabled in RO or if it's enabled in
	 * RO, EC won't jump to RW, pd_prepare_sysjump is not needed. Even if
	 * PD is enabled because the device is not write protected, EFS2 jumps
	 * to RW before PD tasks start. So, there is no states to clean up.
	 *
	 *  Even if EFS2 is enabled, late sysjump can happen when secdata
	 *  kernel is missing or a communication error happens. So, we need to
	 *  check whether PD tasks have started (instead of VBOOT_EFS2, which
	 *  is static).
	 */
	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP) && task_start_called())
		/* Note: must be before i2c module is locked down */
		pd_prepare_sysjump();

#ifdef CONFIG_I2C_CONTROLLER
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
	jdata->jump_tag_total = 0; /* Reset tags */
	jdata->struct_size = sizeof(struct jump_data);

	/* Call other hooks; these may add tags */
	hook_notify(HOOK_SYSJUMP);

	/* Disable interrupts before jump */
	interrupt_disable_all();

#ifdef CONFIG_DMA_CROS
	/* Disable all DMA channels to avoid memory corruption */
	dma_disable_all();
#endif /* CONFIG_DMA_CROS */

	/* Jump to the reset vector */
	resetvec = (void (*)(void))init_addr;
	resetvec();
}

static int is_rw_image(enum ec_image copy)
{
	return copy == EC_IMAGE_RW || copy == EC_IMAGE_RW_B;
}

int system_is_in_rw(void)
{
	return is_rw_image(system_get_image_copy());
}

test_mockable_static int
system_run_image_copy_with_flags(enum ec_image copy, uint32_t add_reset_flags)
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
		if (system_get_image_copy() != EC_IMAGE_RO)
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
		init_addr = *(uintptr_t *)(init);

		/* Make sure the reset vector is inside the destination image */
		if (!IS_ENABLED(EMU_BUILD) &&
		    (init_addr < base || init_addr >= base + get_size(copy)))
			return EC_ERROR_UNKNOWN;
	}

	system_set_reset_flags(add_reset_flags);

	/* If jumping back to RO, we're no longer in the EFS context. */
	if (copy == EC_IMAGE_RO)
		system_clear_reset_flags(EC_RESET_FLAG_EFS);

	CPRINTS("Jumping to image %s (0x%08x)", ec_image_to_string(copy),
		system_get_reset_flags());

	jump_to_image(init_addr);

	/* Should never get here */
	return EC_ERROR_UNKNOWN;
}

test_mockable int system_run_image_copy(enum ec_image copy)
{
	/* No reset flags needed for most jumps */
	return system_run_image_copy_with_flags(copy, 0);
}

enum ec_image system_get_active_copy(void)
{
	uint8_t slot;
	if (system_get_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, &slot))
		slot = EC_IMAGE_RW_A;
	/* This makes it return RW_A by default. For example, this happens when
	 * BBRAM isn't initialized. */
	return slot == EC_IMAGE_RW_B ? slot : EC_IMAGE_RW_A;
}

enum ec_image system_get_update_copy(void)
{
#ifdef CONFIG_VBOOT_EFS /* Not needed for EFS2, which is single-slot. */
	return system_get_active_copy() == EC_IMAGE_RW_A ? EC_IMAGE_RW_B :
							   EC_IMAGE_RW_A;
#else
	return EC_IMAGE_RW_A;
#endif
}

int system_set_active_copy(enum ec_image copy)
{
	return system_set_bbram(SYSTEM_BBRAM_IDX_TRY_SLOT, copy);
}

#ifdef CONFIG_EC_PROTECTED_STORAGE_OFF
/*
 * This is defined in system.c instead of flash.c because it's called even
 * on the boards which don't include flash.o. (e.g. hadoken, stm32l476g-eval)
 */
uint32_t flash_get_rw_offset(enum ec_image copy)
{
#ifdef CONFIG_VBOOT_EFS
	if (copy == EC_IMAGE_RW_B)
		return CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_B_STORAGE_OFF;
#endif
	if (is_rw_image(copy))
		return CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;

	return CONFIG_EC_PROTECTED_STORAGE_OFF + CONFIG_RO_STORAGE_OFF;
}
#endif

const struct image_data *system_get_image_data(enum ec_image copy)
{
	static struct image_data data;

	uintptr_t addr;
	enum ec_image active_copy = system_get_image_copy();

	/* Handle version of current image */
	if (copy == active_copy || copy == EC_IMAGE_UNKNOWN)
		return &current_image_data;
	if (active_copy == EC_IMAGE_UNKNOWN)
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
	crec_flash_lock_mapped_storage(1);
	memcpy(&data, (const void *)addr, sizeof(data));
	crec_flash_lock_mapped_storage(0);
#else
	/* Read the version struct from flash into a buffer. */
	if (crec_flash_read(addr, sizeof(data), (char *)&data))
		return NULL;
#endif

	/* Make sure the version struct cookies match before returning the
	 * version string. */
	if (data.cookie1 == current_image_data.cookie1 &&
	    data.cookie2 == current_image_data.cookie2)
		return &data;

	return NULL;
}

__attribute__((weak)) /* Weird chips may need their own implementations */
const char *
system_get_version(enum ec_image copy)
{
	const struct image_data *data = system_get_image_data(copy);

	return data ? data->version : "";
}

const char *system_get_cros_fwid(enum ec_image copy)
{
	const struct image_data *data;

	if (IS_ENABLED(CONFIG_CROS_FWID_VERSION)) {
		data = system_get_image_data(copy);
		if (data && (data->cookie3 & CROS_EC_IMAGE_DATA_COOKIE3_MASK) ==
				    CROS_EC_IMAGE_DATA_COOKIE3)
			return data->cros_fwid;
	}
	return "";
}

#ifdef CONFIG_ROLLBACK
int32_t system_get_rollback_version(enum ec_image copy)
{
	const struct image_data *data = system_get_image_data(copy);

	return data ? data->rollback_version : -1;
}
#endif

int system_get_image_used(enum ec_image copy)
{
	const struct image_data *data = system_get_image_data(copy);

	return data ? MAX((int)data->size, 0) : 0;
}

/*
 * Overwrite it in board directory in case that we want to read board version
 * in our own way.
 */
__overridable int board_get_version(void)
{
#ifdef CONFIG_BOARD_VERSION_GPIO
	return (!!gpio_get_level(GPIO_BOARD_VERSION1) << 0) |
	       (!!gpio_get_level(GPIO_BOARD_VERSION2) << 1) |
	       (!!gpio_get_level(GPIO_BOARD_VERSION3) << 2);
#else
	return 0;
#endif
}

/*
 * Returns positive board version if successfully retrieved. Otherwise the
 * value is a negative version of an EC return code. Without this optimization
 * multiple boards run out of flash size.
 */
test_mockable int system_get_board_version(void)
{
	int board_id;

	if (IS_ENABLED(CONFIG_BOARD_VERSION_CBI)) {
		int error;

		error = cbi_get_board_version(&board_id);
		if (error)
			return -error;

		return board_id;
	};

	return board_get_version();
}

__attribute__((weak)) /* Weird chips may need their own implementations */
const char *
system_get_build_info(void)
{
	return build_info;
}

void system_common_pre_init(void)
{
	/*
	 * Log panic cause if watchdog caused reset and panic cause
	 * was not already logged. This must happen before calculating
	 * jump_data address because it might change panic pointer.
	 */
	if (system_get_reset_flags() & EC_RESET_FLAG_WATCHDOG) {
		uint32_t reason;
		uint32_t info;
		uint8_t exception;
		struct panic_data *pdata;

		panic_get_reason(&reason, &info, &exception);
		pdata = panic_get_data();

		/* If the panic reason is a watchdog warning, then change
		 * the reason to a regular watchdog reason while preserving
		 * the info and exception from the watchdog warning.
		 */
		if (reason == PANIC_SW_WATCHDOG_WARN)
			panic_set_reason(PANIC_SW_WATCHDOG, info, exception);
		/* The watchdog panic info may have already been initialized by
		 * the watchdog handler, so only set it here if the panic reason
		 * is not a watchdog or the panic info has already been read,
		 * i.e. an old watchdog panic.
		 */
		else if (reason != PANIC_SW_WATCHDOG || !pdata ||
			 pdata->flags & PANIC_DATA_FLAG_OLD_HOSTCMD)
			panic_set_reason(PANIC_SW_WATCHDOG, 0, 0);
	}

	jdata = get_jump_data();

	/*
	 * Check jump data if this is a jump between images.
	 */
	if (jdata->magic == JUMP_DATA_MAGIC && jdata->version >= 1) {
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
			delta = 0; /* No tags in v1, so no need for move */
		else if (jdata->version == 2)
			delta = sizeof(struct jump_data) - JUMP_DATA_SIZE_V2;
		else
			delta = sizeof(struct jump_data) - jdata->struct_size;

		/*
		 * Check if enough space for jump data.
		 * Clear jump data and return if not.
		 */
		if (system_usable_ram_end() < JUMP_DATA_MIN_ADDRESS) {
			/* TODO(b/251190975): This failure should be reported
			 * in the panic data structure for more visibility.
			 */
			memset(jdata, 0, sizeof(struct jump_data));
			return;
		}

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

void system_enter_manual_recovery(void)
{
	system_info_flags |= SYSTEM_IN_MANUAL_RECOVERY;
}

void system_exit_manual_recovery(void)
{
	system_info_flags &= ~SYSTEM_IN_MANUAL_RECOVERY;
}

int system_is_manual_recovery(void)
{
	return system_info_flags & SYSTEM_IN_MANUAL_RECOVERY;
}

void system_set_reboot_at_shutdown(const struct ec_params_reboot_ec *p)
{
	reboot_at_shutdown = *p;
}

const struct ec_params_reboot_ec *system_get_reboot_at_shutdown(void)
{
	return &reboot_at_shutdown;
}

/**
 * Handle a pending reboot command.
 */
static int handle_pending_reboot(struct ec_params_reboot_ec *p)
{
	if (IS_ENABLED(CONFIG_POWER_BUTTON_INIT_IDLE) &&
	    (p->flags & EC_REBOOT_FLAG_CLEAR_AP_IDLE)) {
		CPRINTS("Clearing AP_IDLE");
		chip_save_reset_flags(chip_read_reset_flags() &
				      ~EC_RESET_FLAG_AP_IDLE);
		p->flags &= ~(EC_REBOOT_FLAG_CLEAR_AP_IDLE);
	}

	switch (p->cmd) {
	case EC_REBOOT_CANCEL:
	case EC_REBOOT_NO_OP:
		return EC_SUCCESS;
	case EC_REBOOT_JUMP_RO:
		return system_run_image_copy_with_flags(
			EC_IMAGE_RO, EC_RESET_FLAG_STAY_IN_RO);
	case EC_REBOOT_JUMP_RW:
		return system_run_image_copy(system_get_active_copy());
	case EC_REBOOT_COLD:
	case EC_REBOOT_COLD_AP_OFF:
		/*
		 * Reboot the PD chip(s) as well, but first suspend the ports
		 * if this board has PD tasks running so they don't query the
		 * TCPCs while they reset.
		 */
		if (IS_ENABLED(HAS_TASK_PD_C0)) {
			int port;

			for (port = 0; port < board_get_usb_pd_port_count();
			     port++)
				pd_set_suspend(port, 1);

			/*
			 * Give enough time to apply CC Open and brown out if
			 * we are running with out a battery.
			 */
			msleep(20);
		}

		/* Reset external PD chips. */
		if (IS_ENABLED(HAS_TASK_PDCMD) ||
		    IS_ENABLED(CONFIG_HAS_TASK_PD_INT))
			board_reset_pd_mcu();

		cflush();
		if (p->cmd == EC_REBOOT_COLD_AP_OFF)
			system_reset(SYSTEM_RESET_HARD |
				     SYSTEM_RESET_LEAVE_AP_OFF);
		else
			system_reset(SYSTEM_RESET_HARD);
		/* That shouldn't return... */
		return EC_ERROR_UNKNOWN;
	case EC_REBOOT_DISABLE_JUMP:
		system_disable_jump();
		return EC_SUCCESS;
	case EC_REBOOT_HIBERNATE:
		if (!IS_ENABLED(CONFIG_HIBERNATE))
			return EC_ERROR_INVAL;

		/*
		 * Allow some time for the system to quiesce before entering EC
		 * hibernate.  Otherwise, some stray signals may cause an
		 * immediate wake up.
		 */
		CPRINTS("Waiting 1s before hibernating...");
		msleep(1000);
		CPRINTS("system hibernating");
		system_hibernate(hibernate_seconds, hibernate_microseconds);
		/* That shouldn't return... */
		return EC_ERROR_UNKNOWN;
	default:
		return EC_ERROR_INVAL;
	}
}

test_mockable void system_enter_hibernate(uint32_t seconds,
					  uint32_t microseconds)
{
	if (!IS_ENABLED(CONFIG_HIBERNATE))
		return;

	/*
	 * On ChromeOS devices, if AC is present, don't hibernate.
	 * It might trigger an immediate wake up (since AC is present),
	 * resulting in an AP reboot.
	 * Hibernate when AC is present never occurs in normal circumstances,
	 * this is to prevent an action triggered by developers.
	 * See: b/192259035
	 */
	if (IS_ENABLED(CONFIG_EXTPOWER) &&
	    IS_ENABLED(CONFIG_AP_POWER_CONTROL) && extpower_is_present()) {
		CPRINTS("AC on, skip hibernate");
		return;
	}

	/*
	 * If chipset is already off, then call system_hibernate directly. Else,
	 * let chipset_task bring down the power rails and transition to proper
	 * state before system_hibernate is called.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		system_hibernate(seconds, microseconds);
	else {
		reboot_at_shutdown.cmd = EC_REBOOT_HIBERNATE;
		hibernate_seconds = seconds;
		hibernate_microseconds = microseconds;

		chipset_force_shutdown(CHIPSET_SHUTDOWN_CONSOLE_CMD);
	}
}

/*****************************************************************************/
/* Hooks */

static void system_common_shutdown(void)
{
	if (reboot_at_shutdown.cmd)
		CPRINTF("Reboot at shutdown: %d\n", reboot_at_shutdown.cmd);
	handle_pending_reboot(&reboot_at_shutdown);

	/* Reset cnt on cold boot */
	update_ap_boot_time(RESET_CNT);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, system_common_shutdown,
	     HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Console and Host Commands */

#ifdef CONFIG_CMD_SYSINFO
static int sysinfo(struct ec_response_sysinfo *info)
{
	memset(info, 0, sizeof(*info));

	info->reset_flags = system_get_reset_flags();

	info->current_image = system_get_image_copy();

	if (system_jumped_to_this_image())
		system_info_flags |= SYSTEM_JUMPED_TO_CURRENT_IMAGE;

	if (system_is_locked()) {
		system_info_flags |= SYSTEM_IS_LOCKED;
		if (force_locked)
			system_info_flags |= SYSTEM_IS_FORCE_LOCKED;
		if (!disable_jump)
			system_info_flags |= SYSTEM_JUMP_ENABLED;
	}

	if (reboot_at_shutdown.cmd)
		system_info_flags |= SYSTEM_REBOOT_AT_SHUTDOWN;

	info->flags = system_info_flags;

	return EC_SUCCESS;
}

static int command_sysinfo(int argc, const char **argv)
{
	struct ec_response_sysinfo info;
	int rv;

	rv = sysinfo(&info);
	if (rv != EC_SUCCESS)
		return rv;

	ccprintf("Reset flags: 0x%08x (", info.reset_flags);
	system_print_reset_flags();
	ccprintf(")\n");
	ccprintf("Copy:   %s\n", ec_image_to_string(info.current_image));
	ccprintf("Jumped: %s\n",
		 (info.flags & SYSTEM_JUMPED_TO_CURRENT_IMAGE) ? "yes" : "no");
	ccprintf("Recovery: %s\n",
		 (info.flags & SYSTEM_IN_MANUAL_RECOVERY) ? "yes" : "no");

	ccputs("Flags: ");
	if (info.flags & SYSTEM_IS_LOCKED) {
		ccputs(" locked");
		if (info.flags & SYSTEM_IS_FORCE_LOCKED)
			ccputs(" (forced)");
		if (!(info.flags & SYSTEM_JUMP_ENABLED))
			ccputs(" jump-disabled");
	} else
		ccputs(" unlocked");
	ccputs("\n");

	if (info.flags & SYSTEM_REBOOT_AT_SHUTDOWN)
		ccprintf("Reboot at shutdown: %d\n",
			 !!(info.flags & SYSTEM_REBOOT_AT_SHUTDOWN));

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(sysinfo, command_sysinfo, NULL,
			     "Print system info");

static enum ec_status host_command_sysinfo(struct host_cmd_handler_args *args)
{
	struct ec_response_sysinfo *r = args->response;

	if (sysinfo(r) != EC_SUCCESS)
		return EC_RES_ERROR;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_SYSINFO, host_command_sysinfo,
		     EC_VER_MASK(EC_VER_SYSINFO));
#endif

#ifdef CONFIG_CMD_SCRATCHPAD
static int command_scratchpad(int argc, const char **argv)
{
	int rv = EC_SUCCESS;
	uint32_t scratchpad_value;

	if (argc == 2) {
		char *e;
		int s = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		rv = system_set_scratchpad(s);

		if (rv) {
			ccprintf("Error setting scratchpad register (%d)\b",
				 rv);
			return rv;
		}
	}

	rv = system_get_scratchpad(&scratchpad_value);
	if (rv)
		ccprintf("Error reading scratchpad register (%d)\n", rv);
	else
		ccprintf("Scratchpad: 0x%08x\n", scratchpad_value);
	return rv;
}
DECLARE_CONSOLE_COMMAND(scratchpad, command_scratchpad, "[val]",
			"Get or set scratchpad value");
#endif /* CONFIG_CMD_SCRATCHPAD */

__maybe_unused static int command_hibernate(int argc, const char **argv)
{
	int seconds = 0;
	int microseconds = 0;

	if (argc >= 2)
		seconds = strtoi(argv[1], NULL, 0);
	if (argc >= 3)
		microseconds = strtoi(argv[2], NULL, 0);

	if (seconds || microseconds) {
		if (IS_ENABLED(CONFIG_HIBERNATE_PSL) &&
		    !IS_ENABLED(NPCX_LCT_SUPPORT)) {
			ccprintf("Hibernating with timeout not supported "
				 "when PSL is enabled.\n");
			return EC_ERROR_INVAL;
		}
		ccprintf("Hibernating for %d.%06d s\n", seconds, microseconds);
	} else
		ccprintf("Hibernating until wake pin asserted.\n");

	system_enter_hibernate(seconds, microseconds);

	return EC_SUCCESS;
}
#ifdef CONFIG_HIBERNATE
DECLARE_CONSOLE_COMMAND(hibernate, command_hibernate, "[sec] [usec]",
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
 */
static void print_build_string(void)
{
	const char *full_build_string;
	const char *p;
	size_t next_token_len;
	size_t line_len = 0;
	const size_t max_line_len = 50;

	ccprintf("Build:\t");
	full_build_string = system_get_build_info();
	p = full_build_string;

	while (*p) {
		/* Print first token */
		if (*p == ' ') {
			next_token_len = strcspn(p + 1, " \0");
			if (next_token_len + line_len > max_line_len) {
				line_len = 0;
				p++;
				ccprintf("\n\t\t");
				continue;
			}
		}
		ccprintf("%c", *p++);
		line_len++;
	}
	ccprintf("\n");
}

static int command_version(int argc, const char **argv)
{
	int board_version;
	const char *fw_version;
	const char *cros_fwid;
	bool __maybe_unused is_active;

	ccprintf("Chip:\t%s %s %s\n", system_get_chip_vendor(),
		 system_get_chip_name(), system_get_chip_revision());

	board_version = system_get_board_version();
	if (board_version < 0)
		ccprintf("Board:\tError %d\n", -board_version);
	else
		ccprintf("Board:\t%d\n", board_version);

	fw_version = system_get_version(EC_IMAGE_RO);
	cros_fwid = system_get_cros_fwid(EC_IMAGE_RO);
	if (IS_ENABLED(CHIP_HAS_RO_B)) {
		is_active = system_get_ro_image_copy() == EC_IMAGE_RO;

		ccprintf("RO_A:\t%s%s\n", is_active ? "* " : "", fw_version);
		if (IS_NONEMPTY_STRING(cros_fwid))
			ccprintf("\t\t%s%s\n", is_active ? "* " : "",
				 cros_fwid);

		is_active = system_get_ro_image_copy() == EC_IMAGE_RO_B;
		fw_version = system_get_version(EC_IMAGE_RO_B);
		cros_fwid = system_get_cros_fwid(EC_IMAGE_RO_B);

		ccprintf("RO_B:\t%s%s\n", is_active ? "* " : "", fw_version);
		if (IS_NONEMPTY_STRING(cros_fwid))
			ccprintf("\t\t%s%s\n", is_active ? "* " : "",
				 cros_fwid);
	} else {
		ccprintf("RO:\t%s\n", fw_version);
		if (IS_NONEMPTY_STRING(cros_fwid))
			ccprintf("\t\t%s\n", cros_fwid);
	}

	fw_version = system_get_version(EC_IMAGE_RW);
	cros_fwid = system_get_cros_fwid(EC_IMAGE_RW);
	if (IS_ENABLED(CONFIG_RW_B)) {
		is_active = system_get_active_copy() == EC_IMAGE_RW;

		ccprintf("RW_A:\t%s%s\n", is_active ? "* " : "", fw_version);
		if (IS_NONEMPTY_STRING(cros_fwid))
			ccprintf("\t\t%s%s\n", is_active ? "* " : "",
				 cros_fwid);

		fw_version = system_get_version(EC_IMAGE_RW_B);
		cros_fwid = system_get_cros_fwid(EC_IMAGE_RW_B);
		is_active = system_get_active_copy() == EC_IMAGE_RW_B;

		ccprintf("RW_B:\t%s%s\n", is_active ? "* " : "", fw_version);
		if (IS_NONEMPTY_STRING(cros_fwid))
			ccprintf("\t\t%s%s\n", is_active ? "* " : "",
				 cros_fwid);
	} else {
		ccprintf("RW:\t%s\n", fw_version);
		if (IS_NONEMPTY_STRING(cros_fwid))
			ccprintf("\t\t%s\n", cros_fwid);
	}

	system_print_extended_version_info();
	print_build_string();

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(version, command_version, NULL, "Print versions");

#ifdef CONFIG_CMD_SYSJUMP
static int command_sysjump(int argc, const char **argv)
{
	uint32_t addr;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	/* Handle named images */
	if (!strcasecmp(argv[1], "RO"))
		return system_run_image_copy_with_flags(
			EC_IMAGE_RO, EC_RESET_FLAG_STAY_IN_RO);
	else if (!strcasecmp(argv[1], "RW") || !strcasecmp(argv[1], "A"))
		return system_run_image_copy(EC_IMAGE_RW);
	else if (!strcasecmp(argv[1], "B")) {
#ifdef CONFIG_RW_B
		return system_run_image_copy(EC_IMAGE_RW_B);
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

static int command_reboot(int argc, const char **argv)
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
		} else if (!strcasecmp(argv[i], "ap-off-in-ro")) {
			flags |= (SYSTEM_RESET_LEAVE_AP_OFF |
				  SYSTEM_RESET_STAY_IN_RO);
		} else if (!strcasecmp(argv[i], "ro")) {
			flags |= SYSTEM_RESET_STAY_IN_RO;
		} else if (!strcasecmp(argv[i], "cancel")) {
			reboot_at_shutdown.cmd = EC_REBOOT_CANCEL;
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
DECLARE_CONSOLE_COMMAND(
	reboot, command_reboot,
	"[hard|soft] [preserve] [ap-off] [wait-ext] [cancel] [ap-off-in-ro]"
	" [ro]",
	"Reboot the EC");

#ifdef CONFIG_CMD_SYSLOCK
static int command_system_lock(int argc, const char **argv)
{
	force_locked = 1;
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(syslock, command_system_lock, NULL,
			     "Lock the system, even if WP is disabled");
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && defined(CONFIG_CMD_SLEEPMASK)
/**
 * Modify and print the sleep mask which controls access to deep sleep
 * mode in the idle task.
 */
static int command_sleepmask(int argc, const char **argv)
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
	ccprintf("sleep mask: %08x\n", (unsigned int)sleep_mask);

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(sleepmask, command_sleepmask,
			     "[ on | off | <sleep_mask>]",
			     "Display/force sleep mask");
#endif

#ifdef CONFIG_CMD_JUMPTAGS
static int command_jumptags(int argc, const char **argv)
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

		ccprintf("%08x: 0x%04x %c%c.%d %3d\n", (uintptr_t)t, t->tag,
			 t->tag >> 8, (uint8_t)t->tag, t->data_version,
			 t->data_size);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(jumptags, command_jumptags, NULL, "List jump tags");
#endif /* CONFIG_CMD_JUMPTAGS */

#ifdef CONFIG_EMULATED_SYSRQ
static int command_sysrq(int argc, const char **argv)
{
	char key = 'x';

	if (argc > 1 && argv[1])
		key = argv[1][0];

	host_send_sysrq(key);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sysrq, command_sysrq, "[key]",
			"Simulate sysrq press (default: x)");
#endif /* CONFIG_EMULATED_SYSRQ */

#ifdef CONFIG_CMD_RESET_FLAGS
static int command_rflags(int argc, const char **argv)
{
	print_reset_flags(chip_read_reset_flags());
	ccprintf("\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rflags, command_rflags, NULL,
			"Print reset flags saved in non-volatile memory");
#endif

/*****************************************************************************/
/* Host commands */

static enum ec_status
host_command_get_version(struct host_cmd_handler_args *args)
{
	struct ec_response_get_version_v1 *r = args->response;
	enum ec_image active_slot = system_get_active_copy();

	strzcpy(r->version_string_ro, system_get_version(EC_IMAGE_RO),
		sizeof(r->version_string_ro));
	strzcpy(r->version_string_rw, system_get_version(active_slot),
		sizeof(r->version_string_rw));

	switch (system_get_image_copy()) {
	case EC_IMAGE_RO:
		r->current_image = EC_IMAGE_RO;
		break;
	case EC_IMAGE_RW:
	case EC_IMAGE_RW_B:
		r->current_image = EC_IMAGE_RW;
		break;
	default:
		r->current_image = EC_IMAGE_UNKNOWN;
		break;
	}

	/*
	 * Assuming args->response is zero'd in host_command_process, so no need
	 * to zero uninitialized fields here.
	 */
	if (args->version > 0 && IS_ENABLED(CONFIG_CROS_FWID_VERSION)) {
		if (args->response_max < sizeof(*r))
			return EC_RES_RESPONSE_TOO_BIG;

		strzcpy(r->cros_fwid_ro, system_get_cros_fwid(EC_IMAGE_RO),
			sizeof(r->cros_fwid_ro));
		strzcpy(r->cros_fwid_rw, system_get_cros_fwid(EC_IMAGE_RW),
			sizeof(r->cros_fwid_rw));
	}

	/*
	 * By convention, ec_response_get_version_v1 is a strict superset of
	 * ec_response_get_version(v0). The v1 response changes the semantics
	 * of one field (reserved to cros_fwid_ro) and adds one additional field
	 * (cros_fwid_rw). So simply adjusting the response size here is safe.
	 */
	if (args->version == 0)
		args->response_size = sizeof(struct ec_response_get_version);
	else if (args->version == 1)
		args->response_size = sizeof(struct ec_response_get_version_v1);
	else
		/* Shouldn't happen because of EC_VER_MASK */
		return EC_RES_INVALID_VERSION;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_VERSION, host_command_get_version,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

#ifdef CONFIG_HOSTCMD_SKUID
static enum ec_status
host_command_get_sku_id(struct host_cmd_handler_args *args)
{
	struct ec_sku_id_info *r = args->response;

	r->sku_id = system_get_sku_id();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_SKU_ID, host_command_get_sku_id,
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
DECLARE_HOST_COMMAND(EC_CMD_SET_SKU_ID, host_command_set_sku_id,
		     EC_VER_MASK(0));
#endif

static enum ec_status
host_command_build_info(struct host_cmd_handler_args *args)
{
	strzcpy(args->response, system_get_build_info(), args->response_max);
	args->response_size = strlen(args->response) + 1;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_BUILD_INFO, host_command_build_info,
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
DECLARE_HOST_COMMAND(EC_CMD_GET_CHIP_INFO, host_command_get_chip_info,
		     EC_VER_MASK(0));

static enum ec_status
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
DECLARE_HOST_COMMAND(EC_CMD_GET_BOARD_VERSION, host_command_get_board_version,
		     EC_VER_MASK(0));

STATIC_IF_NOT(CONFIG_ZTEST)
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
		reboot_at_shutdown.cmd = EC_REBOOT_CANCEL;
		reboot_at_shutdown.flags = 0;
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
		p.flags &= ~(EC_REBOOT_FLAG_ON_AP_SHUTDOWN);
		reboot_at_shutdown = p;
		return EC_RES_SUCCESS;
	}

#ifdef HAS_TASK_HOSTCMD
	if (p.cmd == EC_REBOOT_JUMP_RO || p.cmd == EC_REBOOT_JUMP_RW ||
	    p.cmd == EC_REBOOT_COLD || p.cmd == EC_REBOOT_HIBERNATE ||
	    p.cmd == EC_REBOOT_COLD_AP_OFF) {
		/* Clean busy bits on host for commands that won't return */
#ifndef CONFIG_EC_HOST_CMD
		args->result = EC_RES_SUCCESS;
		host_send_response(args);
#else
		ec_host_cmd_send_response(
			EC_HOST_CMD_SUCCESS,
			(struct ec_host_cmd_handler_args *)args);
#endif
	}
#endif

	CPRINTS("Executing host reboot command %d", p.cmd);
	switch (handle_pending_reboot(&p)) {
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
DECLARE_HOST_COMMAND(EC_CMD_REBOOT_EC, host_command_reboot, EC_VER_MASK(0));

test_mockable int system_can_boot_ap(void)
{
	int soc = -1;
	int pow = -1;

#if defined(CONFIG_BATTERY) && defined(CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON)
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
		return crec_flash_read_pstate_serial();
	else if (IS_ENABLED(CONFIG_OTP))
		return otp_read_serial();
	else
		return "";
}

__overridable int board_write_serial(const char *serialno)
{
	if (IS_ENABLED(CONFIG_FLASH_PSTATE) &&
	    IS_ENABLED(CONFIG_FLASH_PSTATE_BANK))
		return crec_flash_write_pstate_serial(serialno);
	else if (IS_ENABLED(CONFIG_OTP))
		return otp_write_serial(serialno);
	else
		return EC_ERROR_UNIMPLEMENTED;
}
#endif /* CONFIG_SERIALNO_LEN */

#ifdef CONFIG_MAC_ADDR_LEN
/* By default, read MAC address from flash, can be overridden. */
__overridable const char *board_read_mac_addr(void)
{
	if (IS_ENABLED(CONFIG_FLASH_PSTATE) &&
	    IS_ENABLED(CONFIG_FLASH_PSTATE_BANK))
		return crec_flash_read_pstate_mac_addr();
	else
		return "";
}

/* By default, write MAC address from flash, can be overridden. */
__overridable int board_write_mac_addr(const char *mac_addr)
{
	if (IS_ENABLED(CONFIG_FLASH_PSTATE) &&
	    IS_ENABLED(CONFIG_FLASH_PSTATE_BANK))
		return crec_flash_write_pstate_mac_addr(mac_addr);
	else
		return EC_ERROR_UNIMPLEMENTED;
}
#endif /* CONFIG_MAC_ADDR_LEN */

__test_only void system_common_reset_state(void)
{
	jdata = 0;
	reset_flags = 0;
	jumped_to_image = 0;
	system_info_flags = 0;
}

__test_only enum ec_reboot_cmd system_common_get_reset_reboot_at_shutdown(void)
{
	int ret = reboot_at_shutdown.cmd;

	reboot_at_shutdown.cmd = EC_REBOOT_CANCEL;

	return ret;
}
