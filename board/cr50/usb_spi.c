/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "byteorder.h"
#include "ccd_config.h"
#include "cryptoc/sha256.h"
#include "console.h"
#include "dcrypto.h"
#include "extension.h"
#include "gpio.h"
#include "hooks.h"
#include "physical_presence.h"
#include "registers.h"
#include "spi.h"
#include "spi_flash.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"
#include "usb_spi.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

/* Don't hash more than this at once */
#define MAX_SPI_HASH_SIZE (4 * 1024 * 1024)

/*
 * Buffer size to use for reading and hashing.  This must be a multiple of the
 * SHA256 block size (64 bytes) and at least 4 less than the maximum SPI
 * transaction size for H1 (0x80 bytes).  So, 64.
 */
#define SPI_HASH_CHUNK_SIZE 64

/* Timeout for auto-disabling SPI hash device, in microseconds */
#define SPI_HASH_TIMEOUT_US (60 * SECOND)

/* Current device for SPI hashing */
static uint8_t spi_hash_device = USB_SPI_DISABLE;

/*
 * Do we need to use NPCX7 gang programming mode?
 *
 * If 0, then we hold the EC in reset the whole time we've acquired the SPI
 * bus, to keep the EC from accessing it.
 *
 * If 1, then:
 *
 *	When we acquire the EC SPI bus, we need to reset the EC, assert the
 *	gang programmer enable, then take the EC out of reset so its boot ROM
 *	can map the EC's internal SPI bus to the EC gang programmer pins.
 *
 *	When we relinquish the EC SPI bus, we need to reset the EC again while
 *	keeping gang programmer deasserted, then take the EC out of reset.  The
 *	EC will then boot normally.
 */
static uint8_t use_npcx_gang_mode;

/*
 * Device and gang mode selected by last spihash command, for use by
 * spi_hash_pp_done().
 */
static uint8_t new_device;
static uint8_t new_gang_mode;

static void spi_hash_inactive_timeout(void);
DECLARE_DEFERRED(spi_hash_inactive_timeout);

/*****************************************************************************/
/*
 * Mutex and variable for tracking whether the SPI bus is used by the USB
 * connection or hashing commands.
 *
 * Access these ONLY through set_spi_bus_user() and get_spi_bus_user(), to
 * ensure thread-safe access to the SPI bus.
 */
static struct mutex spi_bus_user_mutex;
static enum spi_bus_user_t {
	SPI_BUS_USER_NONE = 0,
	SPI_BUS_USER_USB,
	SPI_BUS_USER_HASH
} spi_bus_user = SPI_BUS_USER_NONE;

/**
 * Set who's using the SPI bus.
 *
 * This is thread-safe and will not block if someone owns the bus.  You can't
 * take the bus if someone else has it, and you can only free it if you hold
 * it.  It has no extra effect if you already own the bus.
 *
 * @param user		What bus user is asking?
 * @param want_bus	Do we want the bus (!=0) or no longer want it (==0)?
 *
 * @return EC_SUCCESS, or non-zero error code.
 */
static int set_spi_bus_user(enum spi_bus_user_t user, int want_bus)
{
	int rv = EC_SUCCESS;

	/*
	 * Serialize access to bus user variable, but don't mutex lock the
	 * entire bus because that would freeze USB or the console instead of
	 * just failing.
	 */
	mutex_lock(&spi_bus_user_mutex);

	if (want_bus) {
		/* Can only take the bus if it's free or we already own it */
		if (spi_bus_user == SPI_BUS_USER_NONE)
			spi_bus_user = user;
		else if (spi_bus_user != user)
			rv = EC_ERROR_BUSY;
	} else {
		/* Can only free the bus if it was ours */
		if (spi_bus_user == user)
			spi_bus_user = SPI_BUS_USER_NONE;
		else
			rv = EC_ERROR_BUSY;
	}

	mutex_unlock(&spi_bus_user_mutex);

	return rv;
}

/**
 * Get the current SPI bus user.
 */
static enum spi_bus_user_t get_spi_bus_user(void)
{
	return spi_bus_user;
}

/*****************************************************************************/
/* Methods to enable / disable the SPI bus and pin mux */

static void disable_ec_ap_spi(void)
{
	int was_ap_spi_en = gpio_get_level(GPIO_AP_FLASH_SELECT);

	/* Disable EC SPI access. */
	gpio_set_level(GPIO_EC_FLASH_SELECT, 0);

	/* Disable AP SPI access. */
	if (was_ap_spi_en) {
		/*
		 * The fact that AP SPI access was enabled means that the EC was
		 * held in reset.  Therefore, it needs to be released here.
		 */
		gpio_set_level(GPIO_AP_FLASH_SELECT, 0);
		deassert_ec_rst();
		deassert_sys_rst();
	}
}

static void enable_ec_spi(void)
{
	/* Select EC flash */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 0);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 1);

	/*
	 * Note that we don't hold the EC in reset here.  This is because some
	 * ECs with internal SPI flash cannot be held in reset in order to
	 * access the flash.
	 */
}

static void enable_ap_spi(void)
{
	/* Select AP flash */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 1);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 0);

	/*
	 * On some systems SYS_RST_L is not level sensitive, so the only way to
	 * be sure we're holding the AP in reset is to hold the EC in reset.
	 */
	assert_ec_rst();
}

/**
 * Enable the pin mux to the SPI master port.
 */
static void enable_spi_pinmux(void)
{
	GWRITE_FIELD(PINMUX, DIOA4_CTL, PD, 0);    /* SPI_MOSI */
	GWRITE_FIELD(PINMUX, DIOA8_CTL, PD, 0);    /* SPI_CLK */

	/* Connect DIO A4, A8, and A14 to the SPI peripheral */
	GWRITE(PINMUX, DIOA4_SEL, 0); /* SPI_MOSI */
	GWRITE(PINMUX, DIOA8_SEL, 0); /* SPI_CS_L */
	GWRITE(PINMUX, DIOA14_SEL, 0); /* SPI_CLK */
	/* Set SPI_CS to be an internal pull up */
	GWRITE_FIELD(PINMUX, DIOA14_CTL, PU, 1);

	CPRINTS("%s: %s", __func__,
		gpio_get_level(GPIO_AP_FLASH_SELECT) ? "AP" : "EC");

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

/**
 * Disable the pin mux to the SPI master port.
 */
static void disable_spi_pinmux(void)
{
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);

	/* Disconnect SPI peripheral to tri-state pads */
	/* Disable internal pull up */
	GWRITE_FIELD(PINMUX, DIOA14_CTL, PU, 0);
	/* TODO: Implement way to get the gpio */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO7_SEL) == GC_PINMUX_DIOA4_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO8_SEL) == GC_PINMUX_DIOA8_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO9_SEL) == GC_PINMUX_DIOA14_SEL);

	GWRITE_FIELD(PINMUX, DIOA4_CTL, PD, 1);    /* SPI_MOSI */
	GWRITE_FIELD(PINMUX, DIOA8_CTL, PD, 1);    /* SPI_CLK */

	/* Set SPI MOSI, CLK, and CS_L as inputs */
	GWRITE(PINMUX, DIOA4_SEL, GC_PINMUX_GPIO0_GPIO7_SEL);
	GWRITE(PINMUX, DIOA8_SEL, GC_PINMUX_GPIO0_GPIO8_SEL);
	GWRITE(PINMUX, DIOA14_SEL, GC_PINMUX_GPIO0_GPIO9_SEL);
}

/*****************************************************************************/
/* USB SPI methods */

int usb_spi_board_enable(struct usb_spi_config const *config)
{
	int host = config->state->enabled_host;

	/* Make sure we're allowed to enable the requested device */
	if (host == USB_SPI_EC) {
		if (!ccd_is_cap_enabled(CCD_CAP_EC_FLASH)) {
			CPRINTS("%s: EC access denied", __func__);
			return EC_ERROR_ACCESS_DENIED;
		}
	} else if (host == USB_SPI_AP) {
		if (!ccd_is_cap_enabled(CCD_CAP_AP_FLASH)) {
			CPRINTS("%s: AP access denied", __func__);
			return EC_ERROR_ACCESS_DENIED;
		}
	} else {
		CPRINTS("%s: device %d not supported", __func__, host);
		return EC_ERROR_INVAL;
	}

	if (set_spi_bus_user(SPI_BUS_USER_USB, 1) != EC_SUCCESS) {
		CPRINTS("%s: bus in use", __func__);
		return EC_ERROR_BUSY;
	}

	disable_ec_ap_spi();

	/*
	 * Only need to check EC vs. AP, because other hosts were ruled out
	 * above.
	 */
	if (host == USB_SPI_EC)
		enable_ec_spi();
	else
		enable_ap_spi();

	enable_spi_pinmux();
	return EC_SUCCESS;
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	CPRINTS("%s", __func__);

	/* Only disable the SPI bus if we own it */
	if (get_spi_bus_user() != SPI_BUS_USER_USB)
		return;

	disable_spi_pinmux();
	disable_ec_ap_spi();
	set_spi_bus_user(SPI_BUS_USER_USB, 0);
}

int usb_spi_interface(struct usb_spi_config const *config,
		      struct usb_setup_packet *req)
{
	if (req->bmRequestType != (USB_DIR_OUT |
				    USB_TYPE_VENDOR |
				    USB_RECIP_INTERFACE))
		return 1;

	if (req->wValue  != 0 ||
	    req->wIndex  != config->interface ||
	    req->wLength != 0)
		return 1;

	if (!config->state->enabled_device)
		return 1;

	switch (req->bRequest) {
	case USB_SPI_REQ_ENABLE_AP:
		config->state->enabled_host = USB_SPI_AP;
		break;
	case USB_SPI_REQ_ENABLE_EC:
		config->state->enabled_host = USB_SPI_EC;
		break;
	case USB_SPI_REQ_ENABLE:
		CPRINTS("%s: Must specify target", __func__);
		/* Fall through... */
	case USB_SPI_REQ_DISABLE:
		config->state->enabled_host = USB_SPI_DISABLE;
		break;

	default:
		return 1;
	}

	/*
	 * Our state has changed, call the deferred function to handle the
	 * state change.
	 */
	hook_call_deferred(config->deferred, 0);
	return 0;
}

/*****************************************************************************/
/* Hashing support */

/**
 * Returns the content of SPI flash
 *
 * @param buf_usr Buffer to write flash contents
 * @param offset Flash offset to start reading from
 * @param bytes Number of bytes to read.
 *
 * @return EC_SUCCESS, or non-zero if any error.
 */
int spi_read_chunk(uint8_t *buf_usr, unsigned int offset, unsigned int bytes)
{
	uint8_t cmd[4];

	if (bytes > SPI_HASH_CHUNK_SIZE)
		return EC_ERROR_INVAL;

	cmd[0] = SPI_FLASH_READ;
	cmd[1] = (offset >> 16) & 0xFF;
	cmd[2] = (offset >> 8) & 0xFF;
	cmd[3] = offset & 0xFF;

	return spi_transaction(SPI_FLASH_DEVICE, cmd, 4, buf_usr, bytes);
}

/**
 * Reset EC out of gang programming mode if needed.
 */
static void spi_hash_stop_ec_device(void)
{
	/* If device is not currently EC, nothing to do */
	if (spi_hash_device != USB_SPI_EC)
		return;

	if (use_npcx_gang_mode) {
		/*
		 * EC was in gang mode.  Pulse reset without asserting gang
		 * programmer enable, so that when we take the EC out of reset
		 * it will boot normally.
		 */
		assert_ec_rst();
		usleep(200);
		use_npcx_gang_mode = 0;
	}

	/*
	 * Release EC from reset (either from above, or because gang progamming
	 * mode was disabled so the EC was held in reset during SPI access).
	 */
	deassert_ec_rst();
}

/**
 * Disable SPI hashing mode.
 *
 * @return Vendor command return code.
 */
static enum vendor_cmd_rc spi_hash_disable(void)
{
	if (spi_hash_device == USB_SPI_DISABLE)
		return VENDOR_RC_SUCCESS;

	/* Can't disable SPI if we don't own it */
	if (get_spi_bus_user() != SPI_BUS_USER_HASH)
		return VENDOR_RC_NOT_ALLOWED;

	/* Disable the SPI bus and chip select */
	disable_spi_pinmux();
	disable_ec_ap_spi();

	/* Stop the EC device, if it was active */
	spi_hash_stop_ec_device();

	/* Release the bus */
	spi_hash_device = USB_SPI_DISABLE;
	new_device = USB_SPI_DISABLE;
	new_gang_mode = 0;
	set_spi_bus_user(SPI_BUS_USER_HASH, 0);

	/* Disable inactivity timer to turn hashing mode off */
	hook_call_deferred(&spi_hash_inactive_timeout_data, -1);

	CPRINTS("%s", __func__);
	return VENDOR_RC_SUCCESS;
}

/**
 * Deferred function to disable SPI hash mode on inactivity.
 */
static void spi_hash_inactive_timeout(void)
{
	spi_hash_disable();
}

/**
 * Callback to set up the new SPI device after physical presence check.
 */
static void spi_hash_pp_done(void)
{
	/* Acquire the bus */
	if (set_spi_bus_user(SPI_BUS_USER_HASH, 1)) {
		CPRINTS("%s: bus busy", __func__);
		return;
	}

	/* Clear previous enable if needed */
	if (spi_hash_device != USB_SPI_DISABLE)
		disable_ec_ap_spi();

	/* Set up new device */
	if (new_device == USB_SPI_AP) {
		/* Stop the EC device, if it was previously active */
		spi_hash_stop_ec_device();

		enable_ap_spi();
	} else {
		/* Force the EC into reset and enable EC SPI bus */
		assert_ec_rst();
		enable_ec_spi();

		/*
		 * If EC is headed into gang programmer mode, need to release
		 * EC from reset after acquiring the bus.  EC_FLASH_SELECT runs
		 * to the EC's GP_SEL_ODL signal, which is what enables gang
		 * programmer mode.
		 */
		if (new_gang_mode) {
			usleep(200);
			deassert_ec_rst();
			use_npcx_gang_mode = 1;
		}
	}

	enable_spi_pinmux();
	spi_hash_device = new_device;

	/* Start inactivity timer to turn hashing mode off */
	hook_call_deferred(&spi_hash_inactive_timeout_data,
			   SPI_HASH_TIMEOUT_US);

	CPRINTS("%s: %s", __func__,
		(spi_hash_device == USB_SPI_AP ? "AP" : "EC"));
}

/* Process vendor subcommand dealing with Physical presence polling. */
static enum vendor_cmd_rc spihash_pp_poll(void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	char *buffer = buf;

	if (spi_hash_device != USB_SPI_DISABLE) {
		buffer[0] = CCD_PP_DONE;
	} else {
		switch (physical_presense_fsm_state()) {
		case PP_AWAITING_PRESS:
			buffer[0] = CCD_PP_AWAITING_PRESS;
			break;
		case PP_BETWEEN_PRESSES:
			buffer[0] = CCD_PP_BETWEEN_PRESSES;
			break;
		default:
			buffer[0] = CCD_PP_CLOSED;
			break;
		}
	}
	*response_size = 1;
	return VENDOR_RC_SUCCESS;
}

/**
 * Set the SPI hashing device.
 *
 * @param dev		Device (enum usb_spi)
 * @param gang_mode	If non-zero, EC uses gang mode
 *
 * @return Vendor command return code
 */
static enum vendor_cmd_rc spi_hash_set_device(int dev, int gang_mode,
					      uint8_t *response_buf,
					      size_t *response_size)
{
	*response_size = 0;

	if (dev == spi_hash_device)
		return VENDOR_RC_SUCCESS;

	/* Enabling requires permission */
	if (!(ccd_is_cap_enabled(CCD_CAP_FLASH_READ)))
		return VENDOR_RC_NOT_ALLOWED;

	new_device = dev;
	new_gang_mode = gang_mode;

	/* Handle enabling */
	if (spi_hash_device == USB_SPI_DISABLE &&
	    !(ccd_is_cap_enabled(CCD_CAP_AP_FLASH) &&
	      ccd_is_cap_enabled(CCD_CAP_EC_FLASH))) {
		/*
		 * We were disabled, and CCD does not grant permission
		 * to both flash chips.  So we need physical presence
		 * to take the SPI bus.  That prevents a malicious
		 * peripheral from using this to reset the device.
		 *
		 * Technically, we could track the chips separately,
		 * and only require physical presence the first time we
		 * check a chip which CCD doesn't grant access to.  But
		 * that's more bookkeeping, so for now the only way to
		 * skip physical presence is to have access to both.
		 */
		int rv = physical_detect_start(0, spi_hash_pp_done);

		if (rv == EC_SUCCESS)
			return VENDOR_RC_IN_PROGRESS;

		*response_size = 1;
		response_buf[0] = rv;

		return VENDOR_RC_INTERNAL_ERROR;
	}

	/*
	 * If we're still here, we already own the SPI bus, and are
	 * changing which chip we're looking at.  Update hash device
	 * directly; no new physical presence required.
	 */
	spi_hash_pp_done();
	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc spi_hash_dump(uint8_t *dest, uint32_t offset,
					uint32_t size)
{
	/* Fail if we don't own the bus */
	if (get_spi_bus_user() != SPI_BUS_USER_HASH) {
		CPRINTS("%s: not enabled", __func__);
		return VENDOR_RC_NOT_ALLOWED;
	}

	/* Bump inactivity timer to turn hashing mode off */
	hook_call_deferred(&spi_hash_inactive_timeout_data,
			   SPI_HASH_TIMEOUT_US);

	if (size > SPI_HASH_MAX_RESPONSE_BYTES)
		return VENDOR_RC_BOGUS_ARGS;

	if (spi_read_chunk(dest, offset, size) != EC_SUCCESS) {
		CPRINTS("%s: read error at 0x%x", __func__, offset);
		return VENDOR_RC_READ_FLASH_FAIL;
	}

	return VENDOR_RC_SUCCESS;
}

static enum vendor_cmd_rc spi_hash_sha256(uint8_t *dest, uint32_t offset,
					  uint32_t size)
{
	HASH_CTX sha;
	uint8_t data[SPI_HASH_CHUNK_SIZE];
	int chunk_size = SPI_HASH_CHUNK_SIZE;
	int chunks = 0;

	/* Fail if we don't own the bus */
	if (get_spi_bus_user() != SPI_BUS_USER_HASH) {
		CPRINTS("%s: not enabled", __func__);
		return VENDOR_RC_NOT_ALLOWED;
	}

	/* Bump inactivity timer to turn hashing mode off */
	hook_call_deferred(&spi_hash_inactive_timeout_data,
			   SPI_HASH_TIMEOUT_US);

	if (size > MAX_SPI_HASH_SIZE)
		return VENDOR_RC_BOGUS_ARGS;

	CPRINTS("%s: 0x%x 0x%x", __func__, offset, size);

	DCRYPTO_SHA256_init(&sha, 0);

	for (chunks = 0; size > 0; chunks++) {
		int this_chunk = MIN(size, chunk_size);
		/* Read the data */
		if (spi_read_chunk(data, offset, this_chunk) != EC_SUCCESS) {
			CPRINTS("%s: read error at 0x%x", __func__, offset);
			return VENDOR_RC_READ_FLASH_FAIL;
		}

		/* Update hash */
		HASH_update(&sha, data, this_chunk);

		/* Give other things a chance to happen */
		if (!(chunks % 128))
			msleep(1);

		size -= this_chunk;
		offset += this_chunk;
	}

	memcpy(dest, HASH_final(&sha), SHA256_DIGEST_SIZE);

	CPRINTS("%s: done", __func__);
	return VENDOR_RC_SUCCESS;
}

/*
 * TPM Vendor command handler for SPI hash commands which need to be available
 * both through CLI and over /dev/tpm0.
 */
static enum vendor_cmd_rc spi_hash_vendor(enum vendor_cmd_cc code,
					  void *buf,
					  size_t input_size,
					  size_t *response_size)
{
	const struct vendor_cc_spi_hash_request *req = buf;
	enum vendor_cmd_rc rc;

	/* Default to no response data */
	*response_size = 0;

	/* Pick what to do based on subcommand. */
	switch (req->subcmd) {
	case SPI_HASH_SUBCMD_DISABLE:
		/* Handle disabling */
		return spi_hash_disable();
	case SPI_HASH_SUBCMD_AP:
		return spi_hash_set_device(USB_SPI_AP, 0, buf, response_size);
	case SPI_HASH_SUBCMD_EC:
		return spi_hash_set_device(USB_SPI_EC,
					   !!(req->flags &
					      SPI_HASH_FLAG_EC_GANG),
					   buf, response_size);
	case SPI_HASH_SUBCMD_SHA256:
		*response_size = SHA256_DIGEST_SIZE;
		rc = spi_hash_sha256(buf, req->offset, req->size);
		if (rc != VENDOR_RC_SUCCESS)
			*response_size = 0;
		return rc;
	case SPI_HASH_SUBCMD_DUMP:
		/* Save size before we overwrite it with data */
		*response_size = req->size;
		rc = spi_hash_dump(buf, req->offset, req->size);
		if (rc != VENDOR_RC_SUCCESS)
			*response_size = 0;
		return rc;
	case SPI_HASH_PP_POLL:
		return spihash_pp_poll(buf, input_size, response_size);

	default:
		CPRINTS("%s:%d - unknown subcommand %d",
			__func__, __LINE__, req->subcmd);
		*response_size = 0;
		return VENDOR_RC_NO_SUCH_SUBCOMMAND;
	}
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_SPI_HASH, spi_hash_vendor);

/**
 * Wrapper for hash commands which are passed through the TPM task context.
 */
static int hash_command_wrapper(int argc, char *argv[])
{
	int rv;
	struct vendor_cc_spi_hash_request req;
	struct tpm_cmd_header *tpm_header;
	const size_t command_size = sizeof(*tpm_header) +
			MAX(sizeof(req), SPI_HASH_MAX_RESPONSE_BYTES);
	uint8_t buf[command_size];
	uint8_t *p;
	uint32_t return_code;

	/* If no args, just return */
	if (argc < 2) {
		ccprintf("SPI hash device: %s\n",
			 (spi_hash_device ?
			  (spi_hash_device == USB_SPI_AP ? "AP" : "EC") :
			  "disable"));
		return EC_SUCCESS;
	}

	/* Parse args into stack-based struct */
	memset(&req, 0, sizeof(req));
	if (!strcasecmp(argv[1], "AP")) {
		req.subcmd = SPI_HASH_SUBCMD_AP;
	} else if (!strcasecmp(argv[1], "EC")) {
		req.subcmd = SPI_HASH_SUBCMD_EC;
		if (argc > 2 && !strcasecmp(argv[2], "gang"))
			req.flags |= SPI_HASH_FLAG_EC_GANG;
	} else if (!strcasecmp(argv[1], "disable")) {
		req.subcmd = SPI_HASH_SUBCMD_DISABLE;
	} else if (argc == 3) {
		req.subcmd = SPI_HASH_SUBCMD_SHA256;
		rv = parse_offset_size(argc, argv, 1, &req.offset, &req.size);
		if (rv)
			return rv;
	} else if (argc == 4 && !strcasecmp(argv[1], "dump")) {
		req.subcmd = SPI_HASH_SUBCMD_DUMP;
		rv = parse_offset_size(argc, argv, 2, &req.offset, &req.size);
		if (rv)
			return rv;
	} else {
		return EC_ERROR_PARAM1;
	}

	/* Build the extension command */
	tpm_header = (struct tpm_cmd_header *)buf;
	tpm_header->tag = htobe16(0x8001); /* TPM_ST_NO_SESSIONS */
	tpm_header->size = htobe32(command_size);
	tpm_header->command_code = htobe32(TPM_CC_VENDOR_BIT_MASK);
	tpm_header->subcommand_code = htobe16(VENDOR_CC_SPI_HASH);
	/* Copy request data */
	p = (uint8_t *)(tpm_header + 1);
	memcpy(p, &req, sizeof(req));

	tpm_alt_extension(tpm_header, command_size);

	/*
	 * Return status in the command code field now, in case of error,
	 * error code is the first byte after the header.
	 */
	return_code = be32toh(tpm_header->command_code);

	if ((return_code != EC_SUCCESS) &&
	    ((return_code - VENDOR_RC_ERR) != VENDOR_RC_IN_PROGRESS)) {
		rv = p[0];
	} else {
		rv = EC_SUCCESS;

		if (req.subcmd == SPI_HASH_SUBCMD_DUMP)
			ccprintf("data: %.*h\n", req.size, p);
		else if (req.subcmd == SPI_HASH_SUBCMD_SHA256)
			ccprintf("hash: %.32h\n", p);
	}

	return rv;
}
DECLARE_SAFE_CONSOLE_COMMAND(spihash, hash_command_wrapper,
		     "ap | ec [gang] | disable | [dump] <offset> <size>",
		     "Hash SPI flash via TPM vendor command");
