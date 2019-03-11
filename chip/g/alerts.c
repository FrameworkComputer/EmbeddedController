/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_id.h"
#include "common.h"
#include "console.h"
#include "endian.h"
#include "extension.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "signed_header.h"
#include "task.h"
#include "tpm_vendor_cmds.h"

#define BROM_FWBIT_APPLYSEC_SC300       0
#define BROM_FWBIT_APPLYSEC_CAMO        1
#define BROM_FWBIT_APPLYSEC_BUSERR      2
#define BROM_FWBIT_APPLYSEC_BUSOBF      3
#define BROM_FWBIT_APPLYSEC_HEARTBEAT   4
#define BROM_FWBIT_APPLYSEC_BATMON      5
#define BROM_FWBIT_APPLYSEC_RTCCHECK    6
#define BROM_FWBIT_APPLYSEC_JITTERY     7
#define BROM_FWBIT_APPLYSEC_TRNG        8
#define BROM_FWBIT_APPLYSEC_VOLT        9
#define BROM_FWBIT_APPLYSEC_NOB5        10
#define BROM_FWBIT_APPLYSEC_UNKNOWN     11

struct alert_desc {
	const char *name;
	const uint8_t fuse; // BROM_FWBIT_APPLYSEC_* fuse that gates the alert
};

// These numbers correspond to index at 'alert_counters/alert_descs' arrays
#define ALERT_NUM_CAMO0_BREACH 0
#define ALERT_NUM_CRYPTO0_DMEM_PARITY 1
#define ALERT_NUM_CRYPTO0_DRF_PARITY 2
#define ALERT_NUM_CRYPTO0_IMEM_PARITY 3
#define ALERT_NUM_CRYPTO0_PGM_FAULT 4
#define ALERT_NUM_DBCTRL_CPU0_D_IF_BUS_ERR 5
#define ALERT_NUM_DBCTRL_CPU0_D_IF_UPDATE_WATCHDOG 6
#define ALERT_NUM_DBCTRL_CPU0_I_IF_BUS_ERR 7
#define ALERT_NUM_DBCTRL_CPU0_I_IF_UPDATE_WATCHDOG 8
#define ALERT_NUM_DBCTRL_CPU0_S_IF_BUS_ERR 9
#define ALERT_NUM_DBCTRL_CPU0_S_IF_UPDATE_WATCHDOG 10
#define ALERT_NUM_DBCTRL_DDMA0_IF_BUS_ERR 11
#define ALERT_NUM_DBCTRL_DDMA0_IF_UPDATE_WATCHDOG 12
#define ALERT_NUM_DBCTRL_DSPS0_IF_BUS_ERR 13
#define ALERT_NUM_DBCTRL_DSPS0_IF_UPDATE_WATCHDOG 14
#define ALERT_NUM_DBCTRL_DUSB0_IF_BUS_ERR 15
#define ALERT_NUM_DBCTRL_DUSB0_IF_UPDATE_WATCHDOG 16
#define ALERT_NUM_FUSE0_FUSE_DEFAULTS 17
#define ALERT_NUM_GLOBALSEC_DIFF_FAIL 18
#define ALERT_NUM_GLOBALSEC_FW0 19
#define ALERT_NUM_GLOBALSEC_FW1 20
#define ALERT_NUM_GLOBALSEC_FW2 21
#define ALERT_NUM_GLOBALSEC_FW3 22
#define ALERT_NUM_GLOBALSEC_HEARTBEAT_FAIL 23
#define ALERT_NUM_GLOBALSEC_PROC_OPCODE_HASH 24
#define ALERT_NUM_GLOBALSEC_SRAM_PARITY_SCRUB 25
#define ALERT_NUM_KEYMGR0_AES_EXEC_CTR_MAX 26
#define ALERT_NUM_KEYMGR0_AES_HKEY 27
#define ALERT_NUM_KEYMGR0_CERT_LOOKUP 28
#define ALERT_NUM_KEYMGR0_FLASH_ENTRY 29
#define ALERT_NUM_KEYMGR0_PW 30
#define ALERT_NUM_KEYMGR0_SHA_EXEC_CTR_MAX 31
#define ALERT_NUM_KEYMGR0_SHA_FAULT 32
#define ALERT_NUM_KEYMGR0_SHA_HKEY 33
#define ALERT_NUM_PMU_BATTERY_MON 34
#define ALERT_NUM_PMU_PMU_WDOG 35
#define ALERT_NUM_RTC0_RTC_DEAD 36
#define ALERT_NUM_TEMP0_MAX_TEMP 37
#define ALERT_NUM_TEMP0_MAX_TEMP_DIFF 38
#define ALERT_NUM_TEMP0_MIN_TEMP 39
#define ALERT_NUM_TRNG0_OUT_OF_SPEC 40
#define ALERT_NUM_TRNG0_TIMEOUT 41
#define ALERT_NUM_VOLT0_VOLT_ERR 42
#define ALERT_NUM_XO0_JITTERY_TRIM_DIS 43

#define ALERTS_NUM 44

uint16_t alert_counters[ALERTS_NUM];

static void alerts_init(void)
{
	int irq;

	// enable every single IRQ for globalsec alerts
	for (irq = GC_IRQNUM_GLOBALSEC_CAMO0_BREACH_ALERT_INT;
		irq <= GC_IRQNUM_GLOBALSEC_XO0_JITTERY_TRIM_DIS_ALERT_INT;
		irq++) {
		task_enable_irq(irq);
	}
}
DECLARE_HOOK(HOOK_INIT, alerts_init, HOOK_PRIO_DEFAULT);

volatile uint32_t *INTR_STATUS_ADDR[] = {
	GREG32_ADDR(GLOBALSEC, ALERT_INTR_STS0),
	GREG32_ADDR(GLOBALSEC, ALERT_INTR_STS1),
};
BUILD_ASSERT(ARRAY_SIZE(INTR_STATUS_ADDR) * 32 >= ALERTS_NUM);

static void alert_intr_clear(int alert)
{
	int reg = alert / 32;
	int offset = alert % 32;

	*INTR_STATUS_ADDR[reg] = 1 << offset;
}

static void alert_interrupt_process(int alert)
{
	alert_counters[alert]++;
	alert_intr_clear(alert);
}

#define GLOBALSEC_ALERT_COUNTER(name) \
	DECLARE_IRQ(GC_IRQNUM_GLOBALSEC_##name##_ALERT_INT, handler_##name, 1); \
	void handler_##name(void) \
	{ \
		alert_interrupt_process(ALERT_NUM_##name); \
	}

GLOBALSEC_ALERT_COUNTER(CAMO0_BREACH);
GLOBALSEC_ALERT_COUNTER(CRYPTO0_DMEM_PARITY);
GLOBALSEC_ALERT_COUNTER(CRYPTO0_DRF_PARITY);
GLOBALSEC_ALERT_COUNTER(CRYPTO0_IMEM_PARITY);
GLOBALSEC_ALERT_COUNTER(CRYPTO0_PGM_FAULT);
GLOBALSEC_ALERT_COUNTER(DBCTRL_CPU0_D_IF_BUS_ERR);
GLOBALSEC_ALERT_COUNTER(DBCTRL_CPU0_D_IF_UPDATE_WATCHDOG);
GLOBALSEC_ALERT_COUNTER(DBCTRL_CPU0_I_IF_BUS_ERR);
GLOBALSEC_ALERT_COUNTER(DBCTRL_CPU0_I_IF_UPDATE_WATCHDOG);
GLOBALSEC_ALERT_COUNTER(DBCTRL_CPU0_S_IF_BUS_ERR);
GLOBALSEC_ALERT_COUNTER(DBCTRL_CPU0_S_IF_UPDATE_WATCHDOG);
GLOBALSEC_ALERT_COUNTER(DBCTRL_DDMA0_IF_BUS_ERR);
GLOBALSEC_ALERT_COUNTER(DBCTRL_DDMA0_IF_UPDATE_WATCHDOG);
GLOBALSEC_ALERT_COUNTER(DBCTRL_DSPS0_IF_BUS_ERR);
GLOBALSEC_ALERT_COUNTER(DBCTRL_DSPS0_IF_UPDATE_WATCHDOG);
GLOBALSEC_ALERT_COUNTER(DBCTRL_DUSB0_IF_BUS_ERR);
GLOBALSEC_ALERT_COUNTER(DBCTRL_DUSB0_IF_UPDATE_WATCHDOG);
GLOBALSEC_ALERT_COUNTER(FUSE0_FUSE_DEFAULTS);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_DIFF_FAIL);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_FW0);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_FW1);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_FW2);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_FW3);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_HEARTBEAT_FAIL);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_PROC_OPCODE_HASH);
GLOBALSEC_ALERT_COUNTER(GLOBALSEC_SRAM_PARITY_SCRUB);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_AES_EXEC_CTR_MAX);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_AES_HKEY);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_CERT_LOOKUP);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_FLASH_ENTRY);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_PW);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_SHA_EXEC_CTR_MAX);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_SHA_FAULT);
GLOBALSEC_ALERT_COUNTER(KEYMGR0_SHA_HKEY);
GLOBALSEC_ALERT_COUNTER(PMU_BATTERY_MON);
GLOBALSEC_ALERT_COUNTER(PMU_PMU_WDOG);
GLOBALSEC_ALERT_COUNTER(RTC0_RTC_DEAD);
GLOBALSEC_ALERT_COUNTER(TEMP0_MAX_TEMP);
GLOBALSEC_ALERT_COUNTER(TEMP0_MAX_TEMP_DIFF);
GLOBALSEC_ALERT_COUNTER(TEMP0_MIN_TEMP);
GLOBALSEC_ALERT_COUNTER(TRNG0_OUT_OF_SPEC);
GLOBALSEC_ALERT_COUNTER(TRNG0_TIMEOUT);
GLOBALSEC_ALERT_COUNTER(VOLT0_VOLT_ERR);
GLOBALSEC_ALERT_COUNTER(XO0_JITTERY_TRIM_DIS);

#define ALERTS_FORMAT_HAVEN 1

struct vc_alerts_data {
	uint16_t version_id;
	uint16_t alerts_num;
	uint16_t counters[ALERTS_NUM];
} __packed;

static enum vendor_cmd_rc vc_get_alerts_data(enum vendor_cmd_cc code,
	void *buf, size_t input_size, size_t *response_size)
{
	int i;
	struct vc_alerts_data *resp = buf;

	if (sizeof(struct vc_alerts_data) > *response_size)
		return VENDOR_RC_RESPONSE_TOO_BIG;

	memset(resp, 0, sizeof(struct vc_alerts_data));
	resp->version_id = htobe16(ALERTS_FORMAT_HAVEN);
	resp->alerts_num = htobe16(ALERTS_NUM);
	for (i = 0; i < ALERTS_NUM; i++) {
		// Most of alert_counters[i] will be zero. We want to avoid
		// disabling IRQ thus check counters with IRQ enabled.
		if (alert_counters[i]) {
			interrupt_disable();
			resp->counters[i] = htobe16(alert_counters[i]);
			alert_counters[i] = 0;
			interrupt_enable();
		}
	}

	*response_size = sizeof(struct vc_alerts_data);

	return VENDOR_RC_SUCCESS;
}
DECLARE_VENDOR_COMMAND(VENDOR_CC_GET_ALERTS_DATA, vc_get_alerts_data);

#ifdef CONFIG_ENABLE_H1_ALERTS_CONSOLE

const struct alert_desc alert_descs[] = {
	{ "camo0/breach", BROM_FWBIT_APPLYSEC_CAMO },
	{ "crypto0/dmem_parity", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "crypto0/drf_parity", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "crypto0/imem_parity", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "crypto0/pgm_fault", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "dbctrl_cpu0_D_if/bus_err", BROM_FWBIT_APPLYSEC_BUSERR },
	{ "dbctrl_cpu0_D_if/update_watchdog", BROM_FWBIT_APPLYSEC_BUSOBF },
	{ "dbctrl_cpu0_I_if/bus_err", BROM_FWBIT_APPLYSEC_BUSERR },
	{ "dbctrl_cpu0_I_if/update_watchdog", BROM_FWBIT_APPLYSEC_BUSOBF },
	{ "dbctrl_cpu0_S_if/bus_err", BROM_FWBIT_APPLYSEC_BUSERR },
	{ "dbctrl_cpu0_S_if/update_watchdog", BROM_FWBIT_APPLYSEC_BUSOBF },
	{ "dbctrl_ddma0_if/bus_err", BROM_FWBIT_APPLYSEC_BUSERR },
	{ "dbctrl_ddma0_if/update_watchdog", BROM_FWBIT_APPLYSEC_BUSOBF },
	{ "dbctrl_dsps0_if/bus_err", BROM_FWBIT_APPLYSEC_BUSERR },
	{ "dbctrl_dsps0_if/update_watchdog", BROM_FWBIT_APPLYSEC_BUSOBF },
	{ "dbctrl_dusb0_if/bus_err", BROM_FWBIT_APPLYSEC_BUSERR },
	{ "dbctrl_dusb0_if/update_watchdog", BROM_FWBIT_APPLYSEC_BUSOBF },
	{ "fuse0/fuse_defaults", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "globalsec/diff_fail", BROM_FWBIT_APPLYSEC_HEARTBEAT },
	{ "globalsec/fw0", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "globalsec/fw1", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "globalsec/fw2", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "globalsec/fw3", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "globalsec/heartbeat_fail", BROM_FWBIT_APPLYSEC_HEARTBEAT },
	{ "globalsec/proc_opcode_hash", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "globalsec/sram_parity_scrub", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/aes_exec_ctr_max", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/aes_hkey", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/cert_lookup", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/flash_entry", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/pw", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/sha_exec_ctr_max", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/sha_fault", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "keymgr0/sha_hkey", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "pmu/battery_mon", BROM_FWBIT_APPLYSEC_BATMON },
	{ "pmu/pmu_wdog", BROM_FWBIT_APPLYSEC_HEARTBEAT },
	{ "rtc0/rtc_dead", BROM_FWBIT_APPLYSEC_RTCCHECK },
	{ "temp0/max_temp", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "temp0/max_temp_diff", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "temp0/min_temp", BROM_FWBIT_APPLYSEC_UNKNOWN },
	{ "trng0/out_of_spec", BROM_FWBIT_APPLYSEC_TRNG },
	{ "trng0/timeout", BROM_FWBIT_APPLYSEC_TRNG },
	{ "volt0/volt_err", BROM_FWBIT_APPLYSEC_VOLT },
	{ "xo0/jittery_trim_dis", BROM_FWBIT_APPLYSEC_JITTERY },
};
BUILD_ASSERT(ARRAY_SIZE(alert_descs) == ALERTS_NUM);

static int alert_intr_status(int alert)
{
	int reg = alert / 32;
	int offset = alert % 32;

	return !!(*INTR_STATUS_ADDR[reg] & BIT(offset));
}

#ifdef CONFIG_BOARD_ID_SUPPORT
static uint32_t fuse_enabled(void)
{
	uint32_t fuses = GR_FUSE(FW_DEFINED_BROM_APPLYSEC);
	// get_current_image_header() is defined in board_id.c and available
	// only when CONFIG_BOARD_ID_SUPPORT is enabled
	const struct SignedHeader *hdr = get_current_image_header();

	return fuses & hdr->applysec_;
}
#else /* CONFIG_BOARD_ID_SUPPORT */
static uint32_t fuse_enabled(void)
{
	return GR_FUSE(FW_DEFINED_BROM_APPLYSEC);
}
#endif /* CONFIG_BOARD_ID_SUPPORT */

static void command_alerts_list(void)
{
	int i;
	uint32_t fuses = fuse_enabled();

	ccprintf("Globalsec alerts status\nColumns:\n"
		" * name\n"
		" * fuse state: '?' - not defined, '#' disabled, '+' enabled\n"
		" * interrupt state\n"
		" * alert counter\n");

	for (i = 0; i < ALERTS_NUM; i++) {
		const char *name = alert_descs[i].name;
		char fuse_status;

		int status = alert_intr_status(i);
		int8_t fuse = alert_descs[i].fuse;

		if (fuse == BROM_FWBIT_APPLYSEC_UNKNOWN)
			fuse_status = '?';
		else if (fuses & BIT(fuse))
			fuse_status = '+';
		else
			fuse_status = '#';

		ccprintf("%32s %c %d %d\n", name, fuse_status, status,
			alert_counters[i]);
		cflush();
	}
}

/* Fire a software enabled alert */
static void command_alerts_fire(int interrupt)
{
	int i = 0;
	int value = 0;

	for (i = 3; i >= 0; i--) {
		/* Trigger register consists of four 2-bit fields.
		 * pair 01 triggers the alerts, pair 10 does not trigger
		 */
		value <<= 2;
		value |= (i == interrupt) ? 1 : 2;
	}
	GWRITE(GLOBALSEC, ALERT_FW_TRIGGER, value); // firing FW-N irq
	GWRITE(GLOBALSEC, ALERT_FW_TRIGGER, 0xaa); // back to normal
}

static int command_alerts(int argc, char **argv)
{
	char *e;

	if (argc == 1) {
		command_alerts_list();
		return EC_SUCCESS;
	}

	if (argc == 3) {
		if (!strcasecmp(argv[1], "fire")) {
			int alert = strtoi(argv[2], &e, 10);

			if (*e || alert < 0 || alert > 3) {
				ccprintf("interrupt number must be in range "
					"[0..3]\n");
				return EC_ERROR_PARAM2;
			}

			command_alerts_fire(alert);
			return EC_SUCCESS;
		}

		return EC_ERROR_PARAM1;
	}

	return EC_ERROR_PARAM_COUNT;
}

DECLARE_CONSOLE_COMMAND(alerts, command_alerts,
	"<|fire [INT]>",
	"View/change alerts status");

#endif /* CONFIG_ENABLE_H1_ALERTS_CONSOLE */
