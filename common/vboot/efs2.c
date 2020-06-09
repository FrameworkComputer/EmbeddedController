/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Early Firmware Selection ver.2.
 *
 * Verify and jump to a RW image. Register boot mode to Cr50.
 */

#include "battery.h"
#include "chipset.h"
#include "clock.h"
#include "compile_time_macros.h"
#include "console.h"
#include "crc8.h"
#include "flash.h"
#include "hooks.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "usb_pd.h"
#include "uart.h"
#include "vboot.h"
#include "vboot_hash.h"

#define CPRINTS(format, args...) cprints(CC_VBOOT,"VB " format, ## args)
#define CPRINTF(format, args...) cprintf(CC_VBOOT,"VB " format, ## args)

static const char *boot_mode_to_string(uint8_t mode)
{
	static const char *boot_mode_str[] = {
		[BOOT_MODE_NORMAL] =		"NORMAL",
		[BOOT_MODE_NO_BOOT] =		"NO_BOOT",
	};
	if (mode < ARRAY_SIZE(boot_mode_str))
		return boot_mode_str[mode];
	return "UNDEF";
}

/*
 * Check whether the session has successfully ended or not. ERR_TIMEOUT is
 * excluded because it's an internal error produced by EC itself.
 */
static bool is_valid_cr50_response(enum cr50_comm_err code)
{
	return code != CR50_COMM_ERR_TIMEOUT
			&& (code >> 8) == CR50_COMM_ERR_PREFIX;
}

static void enable_packet_mode(bool enable)
{
	/*
	 * This can be done by set_flags(INPUT|PULL_UP). We don't need it now
	 * because Cr50 never initiates communication.
	 */
	gpio_set_level(GPIO_PACKET_MODE_EN, enable ? 1 : 0);
}

static enum cr50_comm_err send_to_cr50(const uint8_t *data, size_t size)
{
	timestamp_t until;
	int i, timeout = 0;
	struct cr50_comm_response res = {};

	/* This will wake up (if it's sleeping) and interrupt Cr50. */
	enable_packet_mode(true);

	uart_flush_output();
	uart_clear_input();

	/*
	 * Send packet. No traffic control, assuming Cr50 consumes stream much
	 * faster. TX buffer shouldn't overflow because it's cleared above and
	 * much bigger than the max packet size.
	 *
	 * Disable interrupts so that the data frame will be stored in the Tx
	 * buffer in one piece.
	 */
	interrupt_disable();
	uart_put_raw(data, size);
	interrupt_enable();

	uart_flush_output();

	until.val = get_time().val + CR50_COMM_TIMEOUT;

	/* Make sure console task won't steal the response (in case in the
	 * future we should exchange packets after tasks start). */
	task_disable_task(TASK_ID_CONSOLE);

	/* Wait for response from Cr50 */
	for (i = 0; i < sizeof(res); i++) {
		while (!timeout) {
			int c = uart_getc();
			if (c != -1) {
				res.error = res.error | c << (i*8);
				break;
			}
			msleep(1);
			timeout = timestamp_expired(until, NULL);
		}
	}

	task_enable_task(TASK_ID_CONSOLE);

	/* Exit packet mode */
	enable_packet_mode(false);

	if (timeout) {
		CPRINTS("Timeout");
		return CR50_COMM_ERR_TIMEOUT;
	}

	CPRINTS("Received 0x%04x", res.error);

	return res.error;
}

static enum cr50_comm_err cmd_to_cr50(enum cr50_comm_cmd cmd,
				      const uint8_t *data, size_t size)
{
	/*
	 * This is on the stack instead of .bss because vboot_main currently is
	 * called only once (from main). Keeping the space unused in .bss would
	 * be wasteful.
	 */
	struct {
		uint8_t preamble[CR50_UART_RX_BUFFER_SIZE];
		uint8_t packet[CR50_COMM_MAX_REQUEST_SIZE];
	} __packed s;
	struct cr50_comm_request *p = (struct cr50_comm_request *)s.packet;
	int retry = CR50_COMM_MAX_RETRY;
	enum cr50_comm_err rv;

	/* compose a frame = preamble + packet */
	memset(s.preamble, CR50_COMM_PREAMBLE, sizeof(s.preamble));
	p->magic = CR50_PACKET_MAGIC;
	p->struct_version = CR50_COMM_PACKET_VERSION;
	p->type = cmd;
	p->size = size;
	memcpy(p->data, data, size);
	p->crc = crc8((uint8_t *)&p->type,
		      sizeof(p->type) + sizeof(p->size) + size);

	do {
		rv = send_to_cr50((uint8_t *)&s,
				  sizeof(s.preamble) + sizeof(*p) + p->size);
		if (is_valid_cr50_response(rv))
			break;
		msleep(5);
	} while (--retry);

	return rv;
}

static enum cr50_comm_err verify_hash(void)
{
	const uint8_t *hash;
	int rv;

	/* Wake up Cr50 beforehand in case it's asleep. */
	enable_packet_mode(true);
	CPRINTS("Ping Cr50");
	msleep(1);
	enable_packet_mode(false);

	rv = vboot_get_rw_hash(&hash);
	if (rv)
		return rv;

	CPRINTS("Verifying hash");
	return cmd_to_cr50(CR50_COMM_CMD_VERIFY_HASH, hash, SHA256_DIGEST_SIZE);
}

static enum cr50_comm_err set_boot_mode(uint8_t mode)
{
	enum cr50_comm_err rv;

	CPRINTS("Setting boot mode to %s(%d)", boot_mode_to_string(mode), mode);
	rv = cmd_to_cr50(CR50_COMM_CMD_SET_BOOT_MODE,
			 &mode, sizeof(enum boot_mode));
	if (rv != CR50_COMM_SUCCESS)
		CPRINTS("Failed to set boot mode");
	return rv;
}

static bool pd_comm_enabled;

static void enable_pd(void)
{
	CPRINTS("Enable USB-PD");
	pd_comm_enabled = true;
}

bool vboot_allow_usb_pd(void)
{
	return pd_comm_enabled;
}

__overridable void show_critical_error(void)
{
	CPRINTS("%s", __func__);
}

static void verify_and_jump(void)
{
	enum cr50_comm_err rv = verify_hash();

	switch (rv) {
	case CR50_COMM_ERR_BAD_PAYLOAD:
		/* Cr50 should have set NO_BOOT. */
		CPRINTS("Hash mismatch");
		enable_pd();
		break;
	case CR50_COMM_SUCCESS:
		system_set_reset_flags(EC_RESET_FLAG_EFS);
		rv = system_run_image_copy(EC_IMAGE_RW);
		CPRINTS("Failed to jump (0x%x)", rv);
		system_clear_reset_flags(EC_RESET_FLAG_EFS);
		show_critical_error();
		break;
	default:
		CPRINTS("Failed to verify RW (0x%x)", rv);
		show_critical_error();
	}
}

__overridable void show_power_shortage(void)
{
	CPRINTS("%s", __func__);
}

static bool is_battery_ready(void)
{
	/* TODO: Add battery check (https://crbug.com/1045216) */
	return true;
}

void vboot_main(void)
{
	CPRINTS("Main");

	if (system_is_in_rw()) {
		/*
		 * We come here and immediately return. LED shows power shortage
		 * but it will be immediately corrected if the adapter can
		 * provide enough power.
		 */
		CPRINTS("Already in RW");
		show_power_shortage();
		return;
	}

	if (system_is_manual_recovery() ||
	    (system_get_reset_flags() & EC_RESET_FLAG_STAY_IN_RO)) {
		if (system_is_manual_recovery())
			CPRINTS("In recovery mode");
		if (!IS_ENABLED(CONFIG_BATTERY)
				&& !IS_ENABLED(HAS_TASK_KEYSCAN)) {
			/*
			 * For Chromeboxes, we relax security by allowing PD in
			 * RO. Attackers don't gain meaningful advantage on
			 * built-in-keyboard-less systems.
			 *
			 * Alternatively, we can use NO_BOOT to show a firmware
			 * screen, strictly requiring BJ adapter and keeping PD
			 * disabled.
			 */
			enable_pd();
			return;
		}

		/*
		 * If battery is drained or bad, we will boot in NO_BOOT mode to
		 * inform the user of the problem.
		 */
		if (!is_battery_ready()) {
			CPRINTS("Battery not ready or bad");
			if (set_boot_mode(BOOT_MODE_NO_BOOT) ==
					CR50_COMM_SUCCESS)
				enable_pd();
		}

		/* We'll enter recovery mode immediately, later, or never. */
		return;
	}

	verify_and_jump();

	/*
	 * EFS failed. EC-RO may be able to boot AP if:
	 *
	 *   - Battery is charged or
	 *   - AC adapter supply in RO >= Boot threshold or
	 *   - BJ adapter is plugged.
	 *
	 * Once AP boots, software sync will fix the mismatch. If that's the
	 * reason of the failure, we won't come back here next time.
	 */
	CPRINTS("Exit");
}

void hook_shutdown(void)
{
	CPRINTS("%s", __func__);

	/*
	 * We filter the cases which can be interfered with if we execute
	 * system_reset in HOOK_CHIPSET_SHUTDOWN context. Most cases are
	 * filtered out by system_is_in_rw (e.g. system_common_shutdown,
	 * check_pending_cutoff).
	 */
	if (system_is_in_rw())
		return;

	/*
	 * We can't reset here because it'll completely tear down the power
	 * and disturb the PCH's power sequence. We instead sysjump.
	 *
	 * Note that this does not reduce the security. Even if it's hijacked in
	 * NO_BOOT mode, an RO still needs to go through a cold reset to clear
	 * NO_BOOT flag since Cr50 rejects to switch from NO_BOOT to NORMAL.
	 * If a spoofed matching hash is passed to Cr50, Cr50 would reset EC.
	 */
	system_set_reset_flags(EC_RESET_FLAG_AP_IDLE);
	verify_and_jump();
}
/*
 * There can be hooks which are needed to set external chips to a certain state
 * in S5. If the initial state (i.e. AP_OFF state) is different from what those
 * hooks realize, they need to be considered. This hook runs last (i.e.
 * HOOK_PRIO_LAST) to make our landing on S5 as mild as possible.
 */
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN_COMPLETE, hook_shutdown, HOOK_PRIO_LAST);
