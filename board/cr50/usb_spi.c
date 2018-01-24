/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ccd_config.h"
#include "cryptoc/sha256.h"
#include "console.h"
#include "dcrypto.h"
#include "gpio.h"
#include "hooks.h"
#include "physical_presence.h"
#include "registers.h"
#include "spi.h"
#include "spi_flash.h"
#include "system.h"
#include "task.h"
#include "timer.h"
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

	CPRINTS("usb_spi enable %s",
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
			CPRINTS("EC SPI access denied");
			return EC_ERROR_ACCESS_DENIED;
		}
	} else if (host == USB_SPI_AP) {
		if (!ccd_is_cap_enabled(CCD_CAP_AP_FLASH)) {
			CPRINTS("AP SPI access denied");
			return EC_ERROR_ACCESS_DENIED;
		}
	} else {
		CPRINTS("SPI device not supported");
		return EC_ERROR_INVAL;
	}

	if (set_spi_bus_user(SPI_BUS_USER_USB, 1) != EC_SUCCESS) {
		CPRINTS("SPI bus in use");
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
	CPRINTS("usb_spi disable");

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
		CPRINTS("ERROR: Must specify target");
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
 * @return EC_SUCCESS or non-zero error code.
 */
static int spi_hash_disable(void)
{
	/* Can't disable SPI if we don't own it */
	if (get_spi_bus_user() != SPI_BUS_USER_HASH)
		return EC_ERROR_ACCESS_DENIED;

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

	CPRINTS("SPI hash device: disable\n");
	return EC_SUCCESS;
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
		CPRINTS("spihdev: bus busy");
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

	CPRINTS("SPI hash device: %s",
		(spi_hash_device == USB_SPI_AP ? "AP" : "EC"));
}

static int command_spi_hash_set_device(int argc, char **argv)
{
	new_device = spi_hash_device;
	new_gang_mode = 0;

	/* See if user wants to change the hash device */
	if (argc >= 2) {
		if (!strcasecmp(argv[1], "AP"))
			new_device = USB_SPI_AP;
		else if (!strcasecmp(argv[1], "EC"))
			new_device = USB_SPI_EC;
		else if (!strcasecmp(argv[1], "disable"))
			new_device = USB_SPI_DISABLE;
		else
			return EC_ERROR_PARAM1;
	}

	/* Check for whether to use NPCX gang programmer mode */
	if (argc >= 3) {
		if (new_device == USB_SPI_EC && !strcasecmp(argv[2], "gang"))
			new_gang_mode = 1;
		else
			return EC_ERROR_PARAM2;
	}

	if (new_device != spi_hash_device) {
		/* If we don't have permission, only allow disabling */
		if (new_device != USB_SPI_DISABLE &&
		    !(ccd_is_cap_enabled(CCD_CAP_FLASH_READ)))
			return EC_ERROR_ACCESS_DENIED;

		if (new_device == USB_SPI_DISABLE) {
			/* Disable SPI hashing */
			return spi_hash_disable();
		}

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
			return physical_detect_start(0, spi_hash_pp_done);
		}

		/*
		 * If we're still here, we already own the SPI bus, and are
		 * changing which chip we're looking at.  Update hash device
		 * directly; no new physical presence required.
		 */
		spi_hash_pp_done();
		return EC_SUCCESS;
	}

	ccprintf("SPI hash device: %s\n",
		 (spi_hash_device ?
		  (spi_hash_device == USB_SPI_AP ? "AP" : "EC") : "disable"));
	return EC_SUCCESS;
}

static int command_spi_hash(int argc, char **argv)
{
	HASH_CTX sha;
	int offset = -1;
	int chunk_size = SPI_HASH_CHUNK_SIZE;
	int size = 256;
	int rv = EC_SUCCESS;
	uint8_t data[SPI_HASH_CHUNK_SIZE];
	int dump = 0;
	int chunks = 0;
	int i;

	/* Handle setting/printing the active device */
	if (argc == 1 ||
	    !strcasecmp(argv[1], "AP") ||
	    !strcasecmp(argv[1], "EC") ||
	    !strcasecmp(argv[1], "disable"))
		return command_spi_hash_set_device(argc, argv);

	/* Fail if we don't own the bus */
	if (get_spi_bus_user() != SPI_BUS_USER_HASH) {
		ccprintf("SPI hash not enabled\n");
		return EC_ERROR_ACCESS_DENIED;
	}

	/* Bump inactivity timer to turn hashing mode off */
	hook_call_deferred(&spi_hash_inactive_timeout_data,
			   SPI_HASH_TIMEOUT_US);

	/* Parse args */
	// TODO: parse offset and size directly, since we want them both
	rv = parse_offset_size(argc, argv, 1, &offset, &size);
	if (rv)
		return rv;
	if (argc > 3 && !strcasecmp(argv[3], "dump"))
		dump = 1;

	if (size < 0 || size > MAX_SPI_HASH_SIZE)
		return EC_ERROR_INVAL;

	DCRYPTO_SHA256_init(&sha, 0);

	for (chunks = 0; size > 0; chunks++) {
		int this_chunk = MIN(size, chunk_size);

		/* Read the data */
		rv = spi_read_chunk(data, offset, this_chunk);
		if (rv) {
			ccprintf("Read error at 0x%x\n", offset);
			return rv;
		}

		/* Update hash */
		HASH_update(&sha, data, this_chunk);

		if (dump) {
			/* Also dump it */
			for (i = 0; i < this_chunk; i++) {
				if ((offset + i) % 16) {
					ccprintf(" %02x", data[i]);
				} else {
					ccprintf("\n%08x: %02x",
						 offset + i, data[i]);
					cflush();
				}
			}
			ccputs("\n");
			msleep(1);
		} else {
			/* Print often at first then slow down */
			if (chunks < 16 || !(chunks % 64)) {
				ccputs(".");
				msleep(1);
			}
		}

		size -= this_chunk;
		offset += this_chunk;
	}

	if (!dump) {
		cflush();  /* Make sure there's space for the hash to print */
		ccputs("\n");
	}

	ccprintf("Hash = %.32h\n", HASH_final(&sha));
	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(spihash, command_spi_hash,
		     "ap | ec [gang] | disable | <offset> <size> [dump]",
		     "Hash SPI flash");
