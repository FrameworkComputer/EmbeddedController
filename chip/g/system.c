/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "console.h"
#include "cpu.h"
#include "cpu.h"
#include "flash.h"
#include "printf.h"
#include "registers.h"
#include "signed_header.h"
#include "system.h"
#include "task.h"
#include "version.h"

static void check_reset_cause(void)
{
	uint32_t g_rstsrc = GR_PMU_RSTSRC;
	uint32_t flags = 0;

	/* Clear the reset source now we have recorded it */
	GR_PMU_CLRRST = 1;

	if (g_rstsrc & GC_PMU_RSTSRC_POR_MASK) {
		/* If power-on reset is true, that's the only thing */
		system_set_reset_flags(RESET_FLAG_POWER_ON);
		return;
	}

	/* Low-power exit (ie, wake from deep sleep) */
	if (g_rstsrc & GC_PMU_RSTSRC_EXIT_MASK) {
		/* This register is cleared by reading it */
		uint32_t g_exitpd = GR_PMU_EXITPD_SRC;

		flags |= RESET_FLAG_HIBERNATE;

		if (g_exitpd & GC_PMU_EXITPD_SRC_PIN_PD_EXIT_MASK)
			flags |= RESET_FLAG_WAKE_PIN;
		if (g_exitpd & GC_PMU_EXITPD_SRC_UTMI_SUSPEND_N_MASK)
			flags |= RESET_FLAG_USB_RESUME;
		if (g_exitpd & (GC_PMU_EXITPD_SRC_TIMELS0_PD_EXIT_TIMER0_MASK |
				GC_PMU_EXITPD_SRC_TIMELS0_PD_EXIT_TIMER1_MASK))
			flags |= RESET_FLAG_RTC_ALARM;
		if (g_exitpd & GC_PMU_EXITPD_SRC_RDD0_PD_EXIT_TIMER_MASK)
			flags |= RESET_FLAG_RDD;
		if (g_exitpd & GC_PMU_EXITPD_SRC_RBOX_WAKEUP_MASK)
			flags |= RESET_FLAG_RBOX;
	}

	if (g_rstsrc & GC_PMU_RSTSRC_SOFTWARE_MASK)
		flags |= RESET_FLAG_HARD;

	if (g_rstsrc & GC_PMU_RSTSRC_SYSRESET_MASK)
		flags |= RESET_FLAG_SOFT;

	if (g_rstsrc & GC_PMU_RSTSRC_FST_BRNOUT_MASK)
		flags |= RESET_FLAG_BROWNOUT;

	/*
	 * GC_PMU_RSTSRC_WDOG and GC_PMU_RSTSRC_LOCKUP are considered security
	 * threats. They won't show up as a direct reset cause.
	 */
	if (g_rstsrc & GC_PMU_RSTSRC_SEC_THREAT_MASK)
		flags |= RESET_FLAG_SECURITY;

	if (g_rstsrc && !flags)
		flags |= RESET_FLAG_OTHER;

	system_set_reset_flags(flags);
}

void system_pre_init(void)
{
	check_reset_cause();

	/*
	 * This SoC supports dual "RO" bootloader images. The bootloader locks
	 * the running RW image (us) before jumping to it, but we want to be
	 * sure the active bootloader is also locked. Any images updates must
	 * go into an inactive image location. If it's already locked, this has
	 * no effect.
	 */
	GREG32(GLOBALSEC, FLASH_REGION0_CTRL_CFG_EN) = 0;
}

void system_reset(int flags)
{
	/* TODO: Do we need to handle SYSTEM_RESET_PRESERVE_FLAGS? Doubtful. */
	/* TODO(crosbug.com/p/47289): handle RESET_FLAG_WATCHDOG */

	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

#ifdef BOARD_CR50
	/*
	 * On CR50 we want every reset be hard reset, causing the entire
	 * chromebook to reboot: we don't want the TPM reset while the AP
	 * stays up.
	 */
	GR_PMU_GLOBAL_RESET = GC_PMU_GLOBAL_RESET_KEY;
#else

	if (flags & SYSTEM_RESET_HARD) {
		/* Reset the full microcontroller */
		GR_PMU_GLOBAL_RESET = GC_PMU_GLOBAL_RESET_KEY;
	} else {
		/* Soft reset is also fairly hard, and requires
		 * permission registers to be reset to their initial
		 * state.  To accomplish this, first register a wakeup
		 * timer and then enter lower power mode. */

		/* Low speed timers continue to run in low power mode. */
		GREG32(TIMELS, TIMER1_CONTROL) = 0x1;
		/* Wait for this long. */
		GREG32(TIMELS, TIMER1_LOAD) = 1;
		/* Setup wake-up on Timer1 firing. */
		GREG32(PMU, EXITPD_MASK) =
			GC_PMU_EXITPD_MASK_TIMELS0_PD_EXIT_TIMER1_MASK;

		/* All the components to power cycle. */
		GREG32(PMU, LOW_POWER_DIS) =
			GC_PMU_LOW_POWER_DIS_VDDL_MASK |
			GC_PMU_LOW_POWER_DIS_VDDIOF_MASK |
			GC_PMU_LOW_POWER_DIS_VDDXO_MASK |
			GC_PMU_LOW_POWER_DIS_JTR_RC_MASK;
		/* Start low power sequence. */
		REG_WRITE_MLV(GREG32(PMU, LOW_POWER_DIS),
			GC_PMU_LOW_POWER_DIS_START_MASK,
			GC_PMU_LOW_POWER_DIS_START_LSB,
			1);
	}
#endif  /* ^^^^^^^ BOARD_CR50 Not defined */

	/* Wait for reboot; should never return  */
	asm("wfi");
}

const char *system_get_chip_vendor(void)
{
	return "g";
}

const char *system_get_chip_name(void)
{
	return "cr50";
}

const char *system_get_chip_revision(void)
{
	int build_date = GR_SWDP_BUILD_DATE;
	int build_time = GR_SWDP_BUILD_TIME;

	if ((build_date != GC_SWDP_BUILD_DATE_DEFAULT) ||
	    (build_time != GC_SWDP_BUILD_TIME_DEFAULT))
		return " BUILD MISMATCH!";

	switch (GREAD_FIELD(PMU, CHIP_ID, REVISION)) {
	case 3:
		return "B1";
	case 4:
		return "B2";
	}

	return "B?";
}

/* TODO(crosbug.com/p/33822): Where can we store stuff persistently? */
int system_get_vbnvcontext(uint8_t *block)
{
	return 0;
}

int system_set_vbnvcontext(const uint8_t *block)
{
	return 0;
}

enum system_image_copy_t system_get_ro_image_copy(void)
{
	/*
	 * The bootrom protects the selected bootloader with REGION0,
	 * so we should be able to identify the active RO by seeing which one
	 * is protected.
	 */
	switch (GREG32(GLOBALSEC, FLASH_REGION0_BASE_ADDR)) {
	case CONFIG_PROGRAM_MEMORY_BASE + CONFIG_RO_MEM_OFF:
		return SYSTEM_IMAGE_RO;
	case CONFIG_PROGRAM_MEMORY_BASE + CHIP_RO_B_MEM_OFF:
		return SYSTEM_IMAGE_RO_B;
	}

	return SYSTEM_IMAGE_UNKNOWN;
}

/*
 * The RW images contain version strings. The RO images don't, so we'll make
 * some here.
 */
#define MAX_RO_VER_LEN 48
static char vers_str[MAX_RO_VER_LEN];

const char *system_get_version(enum system_image_copy_t copy)
{
	const struct version_struct *v;
	const struct SignedHeader *h;
	enum system_image_copy_t this_copy;
	uintptr_t vaddr, delta;

	switch (copy) {
	case SYSTEM_IMAGE_RO:
	case SYSTEM_IMAGE_RO_B:
		/* The RO header is the first thing in each flash half */
		vaddr = get_program_memory_addr(copy);
		if (vaddr == INVALID_ADDR)
			break;
		h = (const struct SignedHeader *)vaddr;
		/* Use some fields from the header for the version string */
		snprintf(vers_str, MAX_RO_VER_LEN, "%d.%d.%d/%08x",
			 h->epoch_, h->major_, h->minor_, h->img_chk_);
		return vers_str;

	case SYSTEM_IMAGE_RW:
	case SYSTEM_IMAGE_RW_B:
		/*
		 * This function isn't part of any RO image, so we must be in a
		 * RW image. If the current image is the one we're asked for,
		 * we can just return our version string.
		 */
		this_copy = system_get_image_copy();
		vaddr = get_program_memory_addr(this_copy);
		h = (const struct SignedHeader *)vaddr;
		if (copy == this_copy) {
			snprintf(vers_str, sizeof(vers_str), "%d.%d.%d/%s",
				 h->epoch_, h->major_, h->minor_,
				 version_data.version);
			return vers_str;
		}

		/*
		 * We want the version of the other RW image. The linker script
		 * puts the version string right after the reset vectors, so
		 * it's at the same relative offset. Measure that offset here.
		 */
		delta = (uintptr_t)&version_data - vaddr;

		/* Now look at that offset in the requested image */
		vaddr = get_program_memory_addr(copy);
		if (vaddr == INVALID_ADDR)
			break;
		h = (const struct SignedHeader *)vaddr;
		vaddr += delta;
		v = (const struct version_struct *)vaddr;

		/*
		 * Make sure the version struct cookies match before returning
		 * the version string.
		 */
		if (v->cookie1 == version_data.cookie1 &&
		    v->cookie2 == version_data.cookie2 &&
		    h->magic) { /* Corrupted header's magic is set to zero. */
			snprintf(vers_str, sizeof(vers_str), "%d.%d.%d/%s",
				 h->epoch_, h->major_, h->minor_, v->version);
			return vers_str;
		}
	default:
		break;
	}

	return "Error";
}

#ifdef BOARD_CR50

void system_clear_retry_counter(void)
{
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 1);
	GREG32(PMU, LONG_LIFE_SCRATCH0) = 0;
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 0);
}

/*
 * Check which of the two cr50 RW images is newer, return true if the first
 * image is no older than the second one.
 *
 * Note that RO and RW images use the same header structure. When deciding
 * which image to run, the boot ROM ignores the timestamp, but the cros loader
 * considers the timestamp if all other fields are equal.
 */
static int a_is_newer_than_b(const struct SignedHeader *a,
			     const struct SignedHeader *b)
{
	if (a->epoch_ != b->epoch_)
		return a->epoch_ > b->epoch_;
	if (a->major_ != b->major_)
		return a->major_ > b->major_;
	if (a->minor_ != b->minor_)
		return a->minor_ > b->minor_;

	/* This comparison is not made by ROM. */
	if (a->timestamp_ != b->timestamp_)
		return a->timestamp_ > b->timestamp_;

	return 1; /* All else being equal, consider A to be newer. */
}

/*
 * Corrupt the 'magic' field of the passed in header. This prevents the
 * apparently failing image from being considered as a candidate to load and
 * run on the following reboots.
 */
static int corrupt_header(volatile struct SignedHeader *header)
{
	int rv;
	const char zero[4] = {}; /* value to write to magic. */

	/* Enable RW access to the other header. */
	GREG32(GLOBALSEC, FLASH_REGION6_BASE_ADDR) = (uint32_t) header;
	GREG32(GLOBALSEC, FLASH_REGION6_SIZE) = 1023;
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, RD_EN, 1);
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, WR_EN, 1);

	ccprintf("%s: RW fallback must have happened, magic at %p before: %x\n",
		 __func__, &header->magic, header->magic);

	rv = flash_physical_write((intptr_t)&header->magic -
			     CONFIG_PROGRAM_MEMORY_BASE,
			     sizeof(zero), zero);

	/* Disable W access to the other header. */
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, WR_EN, 0);
	ccprintf("%s: magic after: %x\n",
		 __func__, header->magic);

	return rv;
}

/*
 * Value of the retry counter which, if exceeded, indicates that the currently
 * running RW image is not well and is rebooting before bringing the system
 * manages to come up.
 */
#define RW_BOOT_MAX_RETRY_COUNT 5

/*
 * Check if the current running image is newer. Set the passed in pointer, if
 * supplied, to point to the newer image in case the running image is the
 * older one.
 */
static int current_image_is_newer(struct SignedHeader **newer_image)
{
	struct SignedHeader *me, *other;

	if (system_get_image_copy() == SYSTEM_IMAGE_RW) {
		me = (struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW);
		other = (struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW_B);
	} else {
		me = (struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW_B);
		other = (struct SignedHeader *)
			get_program_memory_addr(SYSTEM_IMAGE_RW);
	}

	if (a_is_newer_than_b(me, other))
		return 1;

	if (newer_image)
		*newer_image = other;
	return 0;
}

int system_rollback_detected(void)
{
	return !current_image_is_newer(NULL);
}

int system_process_retry_counter(void)
{
	unsigned retry_counter;
	struct SignedHeader *newer_image;

	retry_counter = GREG32(PMU, LONG_LIFE_SCRATCH0);
	system_clear_retry_counter();

	ccprintf("%s:retry counter %d\n", __func__, retry_counter);

	if (retry_counter <= RW_BOOT_MAX_RETRY_COUNT)
		return EC_SUCCESS;

	if (current_image_is_newer(&newer_image)) {
		ccprintf("%s: "
			 "this is odd, I am newer, but retry counter was %d\n",
			 __func__, retry_counter);
		return EC_SUCCESS;
	}
	/*
	 * let's corrupt the newer image so that the next restart is happening
	 * straight into the current version.
	 */
	return corrupt_header(newer_image);
}

int system_rolling_reboot_suspected(void)
{
	if (GREG32(PMU, LONG_LIFE_SCRATCH0) > 50) {
		/*
		 * The chip has restarted 50 times without the restart counter
		 * cleared. There must be something wrong going, the chip is
		 * likely in rolling reboot.
		 */
		ccprintf("%s: Try powercycling to clear this condition.\n",
			 __func__);
		return 1;
	}

	return 0;
}
#endif

/* Prepend header version to the current image's build info. */
const char *system_get_build_info(void)
{
	static char combined_build_info[150];

	if (!*combined_build_info) {
		const struct SignedHeader *me;

		me = (struct SignedHeader *)
			get_program_memory_addr(system_get_image_copy());
		snprintf(combined_build_info, sizeof(combined_build_info),
			 "%d.%d.%d/%s",
			 me->epoch_, me->major_, me->minor_, build_info);
	}

	return combined_build_info;
}
