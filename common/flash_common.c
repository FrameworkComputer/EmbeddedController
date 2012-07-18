/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Flash memory module for Chrome EC - common functions */

#include "config.h"
#include "console.h"
#include "flash.h"
#include "host_command.h"
#include "power_button.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "util.h"

#define PERSIST_STATE_VERSION 2
#define MAX_BANKS (CONFIG_FLASH_SIZE / CONFIG_FLASH_BANK_SIZE)
#define PHYSICAL_BANKS (CONFIG_FLASH_PHYSICAL_SIZE / CONFIG_FLASH_BANK_SIZE)

/* Persistent protection state flash offset / size / bank */
#define PSTATE_OFFSET (CONFIG_SECTION_FLASH_PSTATE_OFF - CONFIG_FLASH_BASE)
#define PSTATE_SIZE   CONFIG_SECTION_FLASH_PSTATE_SIZE
#define PSTATE_BANK   (PSTATE_OFFSET / CONFIG_FLASH_BANK_SIZE)

/* Read-only firmware offset and size in units of flash banks */
#define RO_BANK_OFFSET ((CONFIG_SECTION_RO_OFF - CONFIG_FLASH_BASE) \
			/ CONFIG_FLASH_BANK_SIZE)
#define RO_BANK_COUNT  (CONFIG_SECTION_RO_SIZE / CONFIG_FLASH_BANK_SIZE)


/* Flags for persist_state.flags */
/* Protect persist state and RO firmware at boot */
#define PERSIST_FLAG_PROTECT_RO 0x02

/* Persistent protection state - emulates a SPI status register for flashrom */
struct persist_state {
	uint8_t version;            /* Version of this struct */
	uint8_t flags;              /* Lock flags (PERSIST_FLAG_*) */
	uint8_t reserved[2];        /* Reserved; set 0 */
};

int stuck_locked;  /* Is physical flash stuck protected? */

static struct persist_state pstate; /* RAM copy of pstate data */

/* Return non-zero if the write protect pin is asserted */
static int wp_pin_asserted(void)
{
#ifdef BOARD_link
	return write_protect_asserted();
#elif defined(CHIP_stm32)
	/* TODO (vpalatin) : write protect scheme for stm32 */
	/*
	 * Always enable write protect until we have WP pin.  For developer to
	 * unlock WP, please use stm32mon -u and immediately re-program the
	 * pstate sector (so that apply_pstate() has no chance to run).
	 */
	return 1;
#else
	/* Other boards don't have a WP pin */
	return 0;
#endif
}

/* Read persistent state into pstate. */
static int read_pstate(void)
{
#ifdef CHIP_stm32
	memset(&pstate, 0, sizeof(pstate));
	pstate.version = PERSIST_STATE_VERSION;
	return EC_SUCCESS;
#else
	const char *flash_pstate = flash_physical_dataptr(PSTATE_OFFSET);

	if (flash_pstate)
		memcpy(&pstate, flash_pstate, sizeof(pstate));

	/* Sanity-check data and initialize if necessary */
	if (pstate.version != PERSIST_STATE_VERSION || !flash_pstate) {
		memset(&pstate, 0, sizeof(pstate));
		pstate.version = PERSIST_STATE_VERSION;
	}

	return flash_pstate ? EC_SUCCESS : EC_ERROR_UNKNOWN;
#endif /* CHIP_stm32 */
}

/* Write persistent state from pstate, erasing if necessary. */
static int write_pstate(void)
{
#ifdef CHIP_stm32
	return EC_SUCCESS;
#else
	int rv;

	/* Erase pstate */
	/*
	 * TODO: optimize based on current physical flash contents; we can
	 * avoid the erase if we're only changing 1's into 0's.
	 */
	rv = flash_physical_erase(PSTATE_OFFSET, PSTATE_SIZE);
	if (rv)
		return rv;

	/*
	 * Note that if we lose power in here, we'll lose the pstate contents.
	 * That's ok, because it's only possible to write the pstate before
	 * it's protected.
	 */

	/* Rewrite the data */
	return flash_physical_write(PSTATE_OFFSET, sizeof(pstate),
				    (const char *)&pstate);
#endif
}

/* Apply write protect based on persistent state. */
static int apply_pstate(void)
{
	int rv;

	/* If write protect is disabled, nothing to do */
	if (!wp_pin_asserted())
		return EC_SUCCESS;

	/* Read the current persist state from flash */
	rv = read_pstate();
	if (rv)
		return rv;

	/* If flash isn't locked, nothing to do */
	if (!(pstate.flags & PERSIST_FLAG_PROTECT_RO))
		return EC_SUCCESS;

	/* Lock the protection data first */
	flash_physical_set_protect(PSTATE_BANK, 1);

	/* Lock the read-only section whenever pstate is locked */
	flash_physical_set_protect(RO_BANK_OFFSET, RO_BANK_COUNT);

	return EC_SUCCESS;
}

int flash_dataptr(int offset, int size_req, int align, char **ptrp)
{
	if (offset < 0 || size_req < 0 ||
			offset + size_req > CONFIG_FLASH_SIZE ||
			(offset | size_req) & (align - 1))
		return -1;  /* Invalid range */
	if (ptrp)
		*ptrp = flash_physical_dataptr(offset);

	return CONFIG_FLASH_SIZE - offset;
}

int flash_write(int offset, int size, const char *data)
{
	if (flash_dataptr(offset, size, CONFIG_FLASH_WRITE_SIZE, NULL) < 0)
		return EC_ERROR_INVAL;  /* Invalid range */

	return flash_physical_write(offset, size, data);
}

int flash_erase(int offset, int size)
{
	if (flash_dataptr(offset, size, CONFIG_FLASH_ERASE_SIZE, NULL) < 0)
		return EC_ERROR_INVAL;  /* Invalid range */

	return flash_physical_erase(offset, size);
}

int flash_protect_until_reboot(void)
{
	/* Protect the entire flash */
	flash_physical_set_protect(0, CONFIG_FLASH_PHYSICAL_SIZE /
				   CONFIG_FLASH_BANK_SIZE);

	return EC_SUCCESS;
}

int flash_enable_protect(int enable)
{
	int rv;

	/* Fail if write protect block is already locked */
	/* TODO: and in the wrong state... if it's asking to enable and we're
	 * already enabled, we can just succeed... */
	if (flash_physical_get_protect(PSTATE_BANK))
		return EC_ERROR_UNKNOWN;

	/* Read the current persist state from flash */
	rv = read_pstate();
	if (rv)
		return rv;

	/* Set the new flag */
	pstate.flags = enable ? PERSIST_FLAG_PROTECT_RO : 0;

	/* Write the state back to flash */
	rv = write_pstate();
	if (rv)
		return rv;

	/* If unlocking, done now */
	if (!enable)
		return EC_SUCCESS;

	/* Otherwise, we need to protect RO code NOW */
	return apply_pstate();
}

int flash_get_protect(void)
{
	int flags = 0;
	int i;

	/* Read the current persist state from flash */
	read_pstate();
	if (pstate.flags & PERSIST_FLAG_PROTECT_RO)
		flags |= FLASH_PROTECT_RO_AT_BOOT;

	/* Check if write protect pin is asserted now */
	if (wp_pin_asserted())
		flags |= FLASH_PROTECT_PIN_ASSERTED;

	/* Scan flash protection */
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		/* Is this bank part of RO? */
		int is_ro = ((i >= RO_BANK_OFFSET &&
			      i < RO_BANK_OFFSET + RO_BANK_COUNT) ||
			     i == PSTATE_BANK);
		int bank_flag = (is_ro ? FLASH_PROTECT_RO_NOW :
				 FLASH_PROTECT_RW_NOW);

		if (flash_physical_get_protect(i)) {
			/* At least one bank in the region is protected */
			flags |= bank_flag;
		} else if (flags & bank_flag) {
			/* But not all banks in the region! */
			flags |= FLASH_PROTECT_PARTIAL;
		}
	}

	/* Check if any banks were stuck locked at boot */
	if (stuck_locked)
		flags |= FLASH_PROTECT_STUCK_LOCKED;

	return flags;
}

/*****************************************************************************/
/* Initialization */

int flash_pre_init(void)
{
	/* Initialize the physical flash interface */
	if (flash_physical_pre_init() == EC_ERROR_ACCESS_DENIED)
		stuck_locked = 1;

	/* Apply write protect to blocks if needed */
	return apply_pstate();
}

/*****************************************************************************/
/* Console commands */

/*
 * Parse offset and size from command line argv[shift] and argv[shift+1]
 *
 * Default values: If argc<=shift, leaves offset unchanged, returning error if
 * *offset<0.  If argc<shift+1, leaves size unchanged, returning error if
 * *size<0.
 */
static int parse_offset_size(int argc, char **argv, int shift,
			     int *offset, int *size)
{
	char *e;
	int i;

	if (argc > shift) {
		i = (uint32_t)strtoi(argv[shift], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		*offset = i;
	} else if (*offset < 0)
		return EC_ERROR_PARAM_COUNT;

	if (argc > shift + 1) {
		i = (uint32_t)strtoi(argv[shift + 1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		*size = i;
	} else if (*size < 0)
		return EC_ERROR_PARAM_COUNT;

	return EC_SUCCESS;
}

static int command_flash_info(int argc, char **argv)
{
	int i;

	ccprintf("Physical:%4d KB\n", CONFIG_FLASH_PHYSICAL_SIZE / 1024);
	if (flash_physical_size() != CONFIG_FLASH_PHYSICAL_SIZE)
		ccprintf("But chip claims %d KB!\n",
			 flash_physical_size() / 1024);

	ccprintf("Usable:  %4d KB\n", CONFIG_FLASH_SIZE / 1024);
	ccprintf("Write:   %4d B\n", CONFIG_FLASH_WRITE_SIZE);
	ccprintf("Erase:   %4d B\n", CONFIG_FLASH_ERASE_SIZE);
	ccprintf("Protect: %4d B\n", CONFIG_FLASH_BANK_SIZE);

	i = flash_get_protect();
	ccprintf("Flags:  ");
	if (i & FLASH_PROTECT_PIN_ASSERTED)
		ccputs(" wp_asserted");
	if (i & FLASH_PROTECT_RO_AT_BOOT)
		ccputs(" ro_at_boot");
	if (i & FLASH_PROTECT_RO_NOW)
		ccputs(" ro_now");
	if (i & FLASH_PROTECT_RW_NOW)
		ccputs(" rw_now");
	if (i & FLASH_PROTECT_STUCK_LOCKED)
		ccputs(" STUCK");
	if (i & FLASH_PROTECT_PARTIAL)
		ccputs(" PARTIAL");
	ccputs("\n");

	ccputs("Protected now:");
	for (i = 0; i < PHYSICAL_BANKS; i++) {
		if (!(i & 7))
			ccputs(" ");
		ccputs(flash_physical_get_protect(i) ? "Y" : ".");
	}
	ccputs("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(flashinfo, command_flash_info,
			NULL,
			"Print flash info",
			NULL);

static int command_flash_erase(int argc, char **argv)
{
	int offset = -1;
	int size = CONFIG_FLASH_ERASE_SIZE;
	int rv;

	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	ccprintf("Erasing %d bytes at 0x%x...\n", size, offset, offset);
	return flash_erase(offset, size);
}
DECLARE_CONSOLE_COMMAND(flasherase, command_flash_erase,
			"offset [size]",
			"Erase flash",
			NULL);

static int command_flash_write(int argc, char **argv)
{
	int offset = -1;
	int size = CONFIG_FLASH_ERASE_SIZE;
	int rv;
	char *data;
	int i;


	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;

	if (size > shared_mem_size())
		size = shared_mem_size();

	/* Acquire the shared memory buffer */
	rv = shared_mem_acquire(size, 0, &data);
	if (rv) {
		ccputs("Can't get shared mem\n");
		return rv;
	}

	/* Fill the data buffer with a pattern */
	for (i = 0; i < size; i++)
		data[i] = i;

	ccprintf("Writing %d bytes to 0x%x...\n",
		 size, offset, offset);
	rv = flash_write(offset, size, data);

	/* Free the buffer */
	shared_mem_release(data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(flashwrite, command_flash_write,
			"offset [size]",
			"Write pattern to flash",
			NULL);

static int command_flash_wp(int argc, char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "enable"))
		return flash_enable_protect(1);
	else if (!strcasecmp(argv[1], "disable"))
		return flash_enable_protect(0);
	else if (!strcasecmp(argv[1], "now"))
		return flash_protect_until_reboot();
	else
		return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(flashwp, command_flash_wp,
			"<enable | disable | now>",
			"Modify flash write protect",
			NULL);

/*****************************************************************************/
/* Host commands */

static int flash_command_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_flash_info *r =
		(struct ec_response_flash_info *)args->response;

	r->flash_size = CONFIG_FLASH_SIZE;
	r->write_block_size = CONFIG_FLASH_WRITE_SIZE;
	r->erase_block_size = CONFIG_FLASH_ERASE_SIZE;
	r->protect_block_size = CONFIG_FLASH_BANK_SIZE;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_INFO,
		     flash_command_get_info,
		     EC_VER_MASK(0));

static int flash_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_read *p =
		(const struct ec_params_flash_read *)args->params;

	if (flash_dataptr(p->offset, p->size, 1, (char **)&args->response) < 0)
		return EC_RES_ERROR;

	args->response_size = p->size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_READ,
		     flash_command_read,
		     EC_VER_MASK(0));

static int flash_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_write *p =
		(const struct ec_params_flash_write *)args->params;

	if (p->size > sizeof(p->data))
		return EC_RES_INVALID_PARAM;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_write(p->offset, p->size, p->data))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_WRITE,
		     flash_command_write,
		     EC_VER_MASK(0));

static int flash_command_erase(struct host_cmd_handler_args *args)
{
	const struct ec_params_flash_erase *p =
		(const struct ec_params_flash_erase *)args->params;

	if (system_unsafe_to_overwrite(p->offset, p->size))
		return EC_RES_ACCESS_DENIED;

	if (flash_erase(p->offset, p->size))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_ERASE,
		     flash_command_erase,
		     EC_VER_MASK(0));
