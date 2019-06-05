/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "console.h"
#include "endian.h"
#include "extension.h"
#include "flash.h"
#include "flash_info.h"
#include "hooks.h"
#include "signed_header.h"
#include "system.h"
#include "upgrade_fw.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_EXTENSION, format, ## args)

static void deferred_reboot(void)
{
	system_reset(SYSTEM_RESET_MANUALLY_TRIGGERED | SYSTEM_RESET_HARD);
}
DECLARE_DEFERRED(deferred_reboot);

#define MAX_REBOOT_TIMEOUT_MS 1000

/*
 * Verify if the header at the passed in flash offset needs to be restored,
 * and restore it if so. If this is an RO header - enable writing into that RO
 * section (the currently active RO writes can not be enabled).
 *
 * Return true if a corruption was detected and restored.
 */
static int header_restored(uint32_t offset)
{
	struct SignedHeader *header;
	uint32_t new_size;

	header = (struct SignedHeader *)(CONFIG_PROGRAM_MEMORY_BASE + offset);

	new_size = header->image_size;
	if (!(new_size & TOP_IMAGE_SIZE_BIT))
		return 0;

	new_size &= ~TOP_IMAGE_SIZE_BIT;
	/*
	 * Clear only in case the size is sensible (i.e. not set to all
	 * ones).
	 */
	if (new_size > CONFIG_RW_SIZE)
		return 0;

	if ((offset == CONFIG_RO_MEM_OFF) || (offset == CHIP_RO_B_MEM_OFF))
		flash_open_ro_window(offset, sizeof(struct SignedHeader));

	return flash_physical_write(offset + offsetof(struct SignedHeader,
						      image_size),
				    sizeof(header->image_size),
				    (char *)&new_size) == EC_SUCCESS;
}

/*
 * Try restoring inactive RO and RW headers, Return the number of restored
 * headers.
 *
 * Since the RO could come with new keys, we don't want create a situation
 * where the RO is restored and the RW is not (say due to power failure or an
 * exception, etc.). So, restore the RW first, and then the RO. In this case
 * if restoring failed, the currently running RO is still guaranteed to boot
 * and start the currently running RW, so the update could be attempted again.
 */
static uint8_t headers_restored(void)
{
	uint8_t total_restored;

	/* Examine the RW first. */
	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
		total_restored = header_restored(CONFIG_RW_B_MEM_OFF);
	else
		total_restored = header_restored(CONFIG_RW_MEM_OFF);

	/* Now the RO */
	if (system_get_ro_image_copy() == SYSTEM_IMAGE_RO)
		total_restored += header_restored(CHIP_RO_B_MEM_OFF);
	else
		total_restored += header_restored(CONFIG_RO_MEM_OFF);

	return total_restored;
}

/*
 * The TURN_UPDATE_ON command comes with a single parameter, which is a 16 bit
 * integer value of the number of milliseconds to wait before reboot in case
 * there has been an update waiting.
 *
 * Maximum wait time is 1000 ms.
 *
 * If the requested timeout exceeds the allowed maximum, or the command is
 * malformed, a protocol error is returned.
 *
 * If there was no errors, the number of restored headers is returned to the
 * host in a single byte.
 *
 * If at least one header was restored AND the host supplied a nonzero timeout
 * value, the H1 will be reset after this many milliseconds.
 *
 * Sending this command with the zero timeout value results in reporting to
 * the host how many headers were restored, the reset is not triggered.
 */
static enum vendor_cmd_rc turn_update_on(enum vendor_cmd_cc code,
					 void *buf,
					 size_t input_size,
					 size_t *response_size)
{
	uint16_t timeout;
	uint8_t *response;

	/* Just in case. */
	*response_size = 0;

	if (input_size < sizeof(uint16_t)) {
		CPRINTF("%s: incorrect request size %d\n",
			__func__, input_size);
		return VENDOR_RC_BOGUS_ARGS;
	}

	/* Retrieve the requested timeout. */
	memcpy(&timeout, buf, sizeof(timeout));
	timeout = be16toh(timeout);

	if (timeout > MAX_REBOOT_TIMEOUT_MS) {
		CPRINTF("%s: incorrect timeout value %d\n",
			__func__, timeout);
		return VENDOR_RC_BOGUS_ARGS;
	}

	*response_size = 1;
	response = buf;

	*response = headers_restored();
	if (*response && timeout) {
		/*
		 * At least one header was restored, and timeout is not zero,
		 * set up the reboot.
		 */
		CPRINTF("%s: rebooting in %d ms\n", __func__, timeout);
		hook_call_deferred(&deferred_reboot_data, timeout * MSEC);
	}

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_TURN_UPDATE_ON, turn_update_on);

/* This command's implementation is shared with USB updater. */
DECLARE_EXTENSION_COMMAND(EXTENSION_FW_UPGRADE, fw_upgrade_command_handler);
