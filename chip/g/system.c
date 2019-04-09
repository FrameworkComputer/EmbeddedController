/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id.h"
#include "console.h"
#include "cpu.h"
#include "cpu.h"
#include "flash.h"
#include "flash_info.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "system_chip.h"
#include "task.h"
#include "version.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
/*
 * Value of the retry counter which, if exceeded, indicates that the currently
 * running RW image is not well and is rebooting before bringing the system
 * manages to come up.
 */
#define RW_BOOT_MAX_RETRY_COUNT 5

static uint8_t pinhold_on_reset;
static uint8_t rollback_detected_at_boot;

static void check_reset_cause(void)
{
	uint32_t g_rstsrc = GR_PMU_RSTSRC;
	uint32_t flags = 0;

	rollback_detected_at_boot = (GREG32(PMU, LONG_LIFE_SCRATCH0) >
		RW_BOOT_MAX_RETRY_COUNT);

	/* Clear the reset source now we have recorded it */
	GR_PMU_CLRRST = 1;

	if (g_rstsrc & GC_PMU_RSTSRC_POR_MASK) {
		/* If power-on reset is true, that's the only thing */
		system_set_reset_flags(EC_RESET_FLAG_POWER_ON);
		return;
	}

	/* Low-power exit (ie, wake from deep sleep) */
	if (g_rstsrc & GC_PMU_RSTSRC_EXIT_MASK) {
		/* This register is cleared by reading it */
		uint32_t g_exitpd = GR_PMU_EXITPD_SRC;

		flags |= EC_RESET_FLAG_HIBERNATE;

		if (g_exitpd & GC_PMU_EXITPD_SRC_PIN_PD_EXIT_MASK)
			flags |= EC_RESET_FLAG_WAKE_PIN;
		if (g_exitpd & GC_PMU_EXITPD_SRC_UTMI_SUSPEND_N_MASK)
			flags |= EC_RESET_FLAG_USB_RESUME;
		if (g_exitpd & (GC_PMU_EXITPD_SRC_TIMELS0_PD_EXIT_TIMER0_MASK |
				GC_PMU_EXITPD_SRC_TIMELS0_PD_EXIT_TIMER1_MASK))
			flags |= EC_RESET_FLAG_RTC_ALARM;
		if (g_exitpd & GC_PMU_EXITPD_SRC_RDD0_PD_EXIT_TIMER_MASK)
			flags |= EC_RESET_FLAG_RDD;
		if (g_exitpd & GC_PMU_EXITPD_SRC_RBOX_WAKEUP_MASK)
			flags |= EC_RESET_FLAG_RBOX;
	}

	if (g_rstsrc & GC_PMU_RSTSRC_SOFTWARE_MASK)
		flags |= EC_RESET_FLAG_HARD;

	if (g_rstsrc & GC_PMU_RSTSRC_SYSRESET_MASK)
		flags |= EC_RESET_FLAG_SOFT;

	if (g_rstsrc & GC_PMU_RSTSRC_FST_BRNOUT_MASK)
		flags |= EC_RESET_FLAG_BROWNOUT;

	/*
	 * GC_PMU_RSTSRC_WDOG and GC_PMU_RSTSRC_LOCKUP are considered security
	 * threats. They won't show up as a direct reset cause.
	 */
	if (g_rstsrc & GC_PMU_RSTSRC_SEC_THREAT_MASK)
		flags |= EC_RESET_FLAG_SECURITY;

	if (g_rstsrc && !flags)
		flags |= EC_RESET_FLAG_OTHER;

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

void system_pinhold_disengage(void)
{
	GREG32(PINMUX, HOLD) = 0;
}

void system_pinhold_on_reset_enable(void)
{
	pinhold_on_reset = 1;
}

void system_pinhold_on_reset_disable(void)
{
	pinhold_on_reset = 0;
}

void system_reset(int flags)
{
	/* Disable interrupts to avoid task swaps during reboot */
	interrupt_disable();

#if defined(CHIP_FAMILY_CR50)
	/*
	 * Decrement the retry counter on manually triggered reboots.  We were
	 * able to process the console command, therefore we're probably okay.
	 */
	if (flags & SYSTEM_RESET_MANUALLY_TRIGGERED)
		system_decrement_retry_counter();

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

		if (pinhold_on_reset)
			GREG32(PINMUX, HOLD) = 1;

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
#endif  /* ^^^^^^^ CHIP_FAMILY_CR50 Not defined */

	/* Wait for reboot; should never return  */
	while (1)
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

/*
 * There are three versions of B2 H1s outhere in the wild so far: chromebook,
 * poppy and detachable. The following registers are different in those
 * three versions in the following way:
 *
 *   register                chromebook          poppy     detachable
 *--------------------------------------------------------------------
 * RBOX_KEY_COMBO0_VAL          0xc0             0x80        0xc0
 * RBOX_POL_KEY1_IN             0x01             0x00        0x00
 * RBOX_KEY_COMBO0_HOLD         0x00             0x00        0x59
 */

static const struct {
	uint32_t  register_values;
	const char *revision_str;
} rev_map[] = {
	{0xc00100, "B2-C"},	/* Chromebook. */
	{0x800000, "B2-P"},	/* Poppy (a one off actually). */
	{0xc00059, "B2-D"},	/* Detachable. */
};

/* Return a value which allows to identify the fuse setting of this chip. */
static uint32_t get_fuse_set_id(void)
{
	return  (GREAD_FIELD(FUSE, RBOX_KEY_COMBO0_VAL, VAL) << 16) |
		(GREAD_FIELD(FUSE, RBOX_POL_KEY1_IN, VAL) << 8) |
		GREAD_FIELD(FUSE, RBOX_KEY_COMBO0_HOLD, VAL);
}

static const char *get_revision_str(void)
{
	int build_date = GR_SWDP_BUILD_DATE;
	int build_time = GR_SWDP_BUILD_TIME;
	uint32_t register_vals;
	int i;

	if ((build_date != GC_SWDP_BUILD_DATE_DEFAULT) ||
	    (build_time != GC_SWDP_BUILD_TIME_DEFAULT))
		return " BUILD MISMATCH!";

	switch (GREAD_FIELD(PMU, CHIP_ID, REVISION)) {
	case 3:
		return "B1";

	case 4:
		register_vals = get_fuse_set_id();
		for (i = 0; i < ARRAY_SIZE(rev_map); i++)
			if (rev_map[i].register_values == register_vals)
				return rev_map[i].revision_str;

		return "B2-?";
	}

	return "B?";
}

const char *system_get_chip_revision(void)
{
	static const char *revision_str;

	if (!revision_str)
		revision_str = get_revision_str();

	return revision_str;
}

int system_get_chip_unique_id(uint8_t **id)
{
	static uint32_t cached[8];

	if (!cached[3]) { /* generate it if it doesn't exist yet */
		const struct SignedHeader *ro_hdr = (const void *)
			get_program_memory_addr(system_get_ro_image_copy());
		const char *rev = get_revision_str();

		cached[0] = ro_hdr->keyid;
		cached[1] = GREG32(FUSE, DEV_ID0);
		cached[2] = GREG32(FUSE, DEV_ID1);
		strncpy((char *)&cached[3], rev, sizeof(cached[3]));
	}
	*id = (uint8_t *)cached;
	return sizeof(cached);
}

int system_battery_cutoff_support_required(void)
{
	switch (get_fuse_set_id())
	case 0xc00059:
		return 1;

	return 0;
}

int system_get_bbram(enum system_bbram_idx idx, uint8_t *value)
{
	return 0;
}

int system_set_bbram(enum system_bbram_idx idx, uint8_t value)
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
 * TODO(crbug.com/698882): Remove support for version_struct_deprecated once
 * we no longer care about supporting legacy RO.
 */
struct version_struct_deprecated {
	uint32_t cookie1;
	char version[32];
	uint32_t cookie2;
};

#define CROS_EC_IMAGE_DATA_COOKIE1_DEPRECATED 0xce112233
#define CROS_EC_IMAGE_DATA_COOKIE2_DEPRECATED 0xce445566

/*
 * The RW images contain version strings. The RO images don't, so we'll make
 * some here.
 */
#define MAX_RO_VER_LEN 48
static char vers_str[MAX_RO_VER_LEN];

const char *system_get_version(enum system_image_copy_t copy)
{
	const struct image_data *data;
	const struct version_struct_deprecated *data_deprecated;
	const char *version;

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
				 current_image_data.version);
			return vers_str;
		}

		/*
		 * We want the version of the other RW image. The linker script
		 * puts the version string right after the reset vectors, so
		 * it's at the same relative offset. Measure that offset here.
		 */
		delta = (uintptr_t)&current_image_data - vaddr;

		/* Now look at that offset in the requested image */
		vaddr = get_program_memory_addr(copy);
		if (vaddr == INVALID_ADDR)
			break;
		h = (const struct SignedHeader *)vaddr;
		/* Corrupted header's magic is set to zero. */
		if (!h->magic)
			break;

		vaddr += delta;
		data = (const struct image_data *)vaddr;
		data_deprecated = (const struct version_struct_deprecated *)
				  vaddr;

		/*
		 * Make sure the version struct cookies match before returning
		 * the version string.
		 */
		if (data->cookie1 == current_image_data.cookie1 &&
		    data->cookie2 == current_image_data.cookie2)
			version = data->version;
		/* Check for old / deprecated structure. */
		else if (data_deprecated->cookie1 ==
			 CROS_EC_IMAGE_DATA_COOKIE1_DEPRECATED &&
			 data_deprecated->cookie2 ==
			 CROS_EC_IMAGE_DATA_COOKIE2_DEPRECATED)
			version = data_deprecated->version;
		else
			break;

		snprintf(vers_str, sizeof(vers_str), "%d.%d.%d/%s",
			 h->epoch_, h->major_, h->minor_, version);
		return vers_str;

	default:
		break;
	}

	return "Error";
}

#if defined(CHIP_FAMILY_CR50)
void system_clear_retry_counter(void)
{
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 1);
	GREG32(PMU, LONG_LIFE_SCRATCH0) = 0;
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 0);
}

void system_decrement_retry_counter(void)
{
	uint32_t val = GREG32(PMU, LONG_LIFE_SCRATCH0);

	if (val != 0) {
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 1);
		GREG32(PMU, LONG_LIFE_SCRATCH0) = val - 1;
		GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 0);
	}
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

/* Used to track if cr50 has corrupted the inactive header */
static uint8_t header_corrupted;

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

	CPRINTS("%s: RW fallback must have happened, magic at %p before: %x",
		__func__, &header->magic, header->magic);

	rv = flash_physical_write((intptr_t)&header->magic -
			     CONFIG_PROGRAM_MEMORY_BASE,
			     sizeof(zero), zero);

	/* Disable W access to the other header. */
	GWRITE_FIELD(GLOBALSEC, FLASH_REGION6_CTRL, WR_EN, 0);
	CPRINTS("%s: magic after: %x", __func__, header->magic);

	header_corrupted = !rv;
	return rv;
}
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
	return rollback_detected_at_boot;
}

int system_process_retry_counter(void)
{
	struct SignedHeader *newer_image;

	CPRINTS("%s: retry counter %d", __func__,
		GREG32(PMU, LONG_LIFE_SCRATCH0));
	system_clear_retry_counter();

	if (!system_rollback_detected())
		return EC_SUCCESS;

	if (current_image_is_newer(&newer_image)) {
		CPRINTS("%s: "
			"this is odd, I am newer, but retry counter indicates "
			"the system rolledback", __func__);
		return EC_SUCCESS;
	}

	if (header_corrupted) {
		CPRINTS("%s: header already corrupted", __func__);
		return EC_SUCCESS;
	}

	/*
	 * let's corrupt the newer image so that the next restart is happening
	 * straight into the current version.
	 */
	return corrupt_header(newer_image);
}

void system_ensure_rollback(void)
{
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 1);
	GREG32(PMU, LONG_LIFE_SCRATCH0) = RW_BOOT_MAX_RETRY_COUNT + 1;
	GWRITE_FIELD(PMU, LONG_LIFE_SCRATCH_WR_EN, REG0, 0);
}

int system_rolling_reboot_suspected(void)
{
	if (GREG32(PMU, LONG_LIFE_SCRATCH0) > 50) {
		/*
		 * The chip has restarted 50 times without the restart counter
		 * cleared. There must be something wrong going, the chip is
		 * likely in rolling reboot.
		 */
		CPRINTS("%s: Try powercycling to clear this condition.",
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

/**
 * Modify info1 RW rollback mask to match the passed in header(s).
 *
 * If both headers' addressses are passed in, the INFO1 rollback mask field is
 * erased in case both headers have a zero in the appropriate bit. If only one
 * header address is passed (the other one is set to zero), only the valid
 * header is considered when updating INFO1.
 */
static void update_rollback_mask(const struct SignedHeader *header_a,
				 const struct SignedHeader *header_b)
{
#ifndef CR50_DEV
	int updated_words_count = 0;
	int i;
	int write_enabled = 0;
	uint32_t header_mask = 0;

	/*
	 * The infomap field in the image header has a matching space in the
	 * flash INFO1 section.
	 *
	 * The INFO1 space words which map into zeroed bits in the infomap
	 * header are ignored by the RO.
	 *
	 * Let's make sure that those words in the INFO1 space are erased.
	 * This in turn makes sure that attempts to load earlier RW images
	 * (where those bits in the header are not zeroed) will fail, thus
	 * ensuring rollback protection.
	 */
	/* For each bit in the header infomap field of the running image. */
	for (i = 0; i < INFO_MAX; i++) {
		uint32_t bit;
		uint32_t word;
		int byte_offset;

		/* Read the next infomap word when done with the current one. */
		if (!(i % 32)) {
			/*
			 * Not to shoot ourselves in the foot, let's zero only
			 * those words in the INFO1 space which are set to
			 * zero in all headers we are supposed to look at.
			 */
			header_mask = 0;

			if (header_a)
				header_mask |= header_a->infomap[i/32];

			if (header_b)
				header_mask |= header_b->infomap[i/32];
		}

		/* Get the next bit value. */
		bit = !!(header_mask & (1 << (i % 32)));
		if (bit) {
			/*
			 * By convention zeroed bits are expected to be
			 * adjacent at the LSB of the info mask field. Stop as
			 * soon as a non-zeroed bit is encountered.
			 */
			CPRINTS("%s: bailing out at bit %d", __func__, i);
			break;
		}

		byte_offset = (INFO_MAX + i) * sizeof(uint32_t);

		if (flash_physical_info_read_word(byte_offset, &word) !=
		    EC_SUCCESS) {
			CPRINTS("failed to read info mask word %d", i);
			continue;
		}

		if (!word)
			continue; /* This word has been zeroed already. */

		if (!write_enabled) {
			flash_info_write_enable();
			write_enabled = 1;
		}

		word = 0;
		if (flash_info_physical_write(byte_offset,
					      sizeof(word),
					      (const char *) &word) !=
		    EC_SUCCESS) {
			CPRINTS("failed to write info mask word %d", i);
			continue;
		}
		updated_words_count++;

	}
	if (!write_enabled)
		return;

	flash_info_write_disable();
	CPRINTS("updated %d info map words", updated_words_count);
#endif  /*  CR50_DEV ^^^^^^^^ NOT defined. */
}

void system_update_rollback_mask_with_active_img(void)
{
	update_rollback_mask((const struct SignedHeader *)
			     get_program_memory_addr(system_get_image_copy()),
			     0);
}

void system_update_rollback_mask_with_both_imgs(void)
{
	update_rollback_mask((const struct SignedHeader *)
			     get_program_memory_addr(SYSTEM_IMAGE_RW),
			     (const struct SignedHeader *)
			     get_program_memory_addr(SYSTEM_IMAGE_RW_B));
}

void system_get_rollback_bits(char *value, size_t value_size)
{
	int info_count;
	int i;
	struct {
		int count;
		const struct SignedHeader *h;
	} headers[] = {
		{.h = (const struct SignedHeader *)
		 get_program_memory_addr(SYSTEM_IMAGE_RW)},

		{.h = (const struct SignedHeader *)
		 get_program_memory_addr(SYSTEM_IMAGE_RW_B)},
	};

	for (i = 0; i < INFO_MAX; i++) {
		uint32_t w;

		flash_physical_info_read_word(INFO_RW_MAP_OFFSET +
					      i * sizeof(uint32_t),
					      &w);
		if (w)
			break;
	}
	info_count = i;

	for (i = 0; i < ARRAY_SIZE(headers); i++) {
		int j;

		for (j = 0; j < INFO_MAX; j++)
			if (headers[i].h->infomap[j/32] & (1 << (j%32)))
				break;
		headers[i].count = j;
	}

	snprintf(value, value_size, "%d/%d/%d", info_count,
		 headers[0].count, headers[1].count);
}

#ifdef CONFIG_EXTENDED_VERSION_INFO

void system_print_extended_version_info(void)
{
	int i;
	struct board_id bid;
	enum system_image_copy_t rw_images[] = {
		SYSTEM_IMAGE_RW, SYSTEM_IMAGE_RW_B
	};

	if (read_board_id(&bid) != EC_SUCCESS) {
		ccprintf("Board ID read failure!\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(rw_images); i++) {
		struct SignedHeader *ss = (struct SignedHeader *)
			get_program_memory_addr(rw_images[i]);

		ccprintf("BID %c:   %08x:%08x:%08x %s\n", 'A' + i,
			 ss->board_id_type ^ SIGNED_HEADER_PADDING,
			 ss->board_id_type_mask ^ SIGNED_HEADER_PADDING,
			 ss->board_id_flags ^ SIGNED_HEADER_PADDING,
			 check_board_id_vs_header(&bid, ss) ? " No" : "Yes");
	}
}

#endif /* CONFIG_EXTENDED_VERSION_INFO */
