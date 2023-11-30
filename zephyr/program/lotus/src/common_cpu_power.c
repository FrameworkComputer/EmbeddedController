


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "driver/sb_rmi.h"
#include "extpower.h"
#include "hooks.h"
#include "math_util.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

struct power_limit_details power_limit[FUNCTION_COUNT];
static int apu_ready;
int target_func[TYPE_COUNT];
bool manual_ctl;
bool safety_pwr_logging;
int mode_ctl;
/* disable b[1:1] to disable power table */
uint8_t func_ctl = 0xff;
int my_test_current;

static int update_sustained_power_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD, msgIn, &msgOut);
}

static int update_fast_ppt_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_FAST_PPT_LIMIT_CMD, msgIn, &msgOut);
}

static int update_slow_ppt_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD, msgIn, &msgOut);
}

static int update_peak_package_power_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_P3T_LIMIT_CMD, msgIn, &msgOut);
}

void update_apu_ready(int status)
{
	apu_ready = status;
}

int get_apu_ready(void)
{
	return apu_ready;
}

static void clear_apu_ready(void)
{
	update_apu_ready(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, clear_apu_ready, HOOK_PRIO_DEFAULT);

static void warmboot_clear_api_ready(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		clear_apu_ready();
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, warmboot_clear_api_ready, HOOK_PRIO_DEFAULT);

int set_pl_limits(uint32_t spl, uint32_t fppt, uint32_t sppt, uint32_t p3t)
{
	RETURN_ERROR(update_sustained_power_limit(spl));
	RETURN_ERROR(update_fast_ppt_limit(fppt));
	RETURN_ERROR(update_slow_ppt_limit(sppt));
	RETURN_ERROR(update_peak_package_power_limit(p3t));
	return EC_SUCCESS;
}

#ifdef CONFIG_BOARD_LOTUS
int update_apu_only_sppt_limit(uint32_t mwatt)
{
	uint32_t msgIn = 0;
	uint32_t msgOut;

	msgIn = mwatt;

	return sb_rmi_mailbox_xfer(SB_RMI_WRITE_APU_ONLY_SPPT_CMD, msgIn, &msgOut);
}
#endif

void update_soc_power_limit_hook(void)
{
	if (!manual_ctl)
		update_soc_power_limit(false, false);
}
DECLARE_HOOK(HOOK_SECOND, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);

static int cmd_cpupower(int argc, const char **argv)
{
	uint32_t spl, fppt, sppt, p3t;
	char *e;

	CPRINTF("Now SOC Power Limit:\n FUNC = %d, SPL %dmW,\n",
		target_func[TYPE_SPL], power_limit[target_func[TYPE_SPL]].mwatt[TYPE_SPL]);
	CPRINTF("FUNC = %d, fPPT %dmW,\n FUNC = %d, sPPT %dmW,\n FUNC = %d, p3T %dmW,\n",
		target_func[TYPE_SPPT], power_limit[target_func[TYPE_SPPT]].mwatt[TYPE_SPPT],
		target_func[TYPE_FPPT], power_limit[target_func[TYPE_FPPT]].mwatt[TYPE_FPPT],
		target_func[TYPE_P3T], power_limit[target_func[TYPE_P3T]].mwatt[TYPE_P3T]);

#ifdef CONFIG_BOARD_LOTUS
	CPRINTF("FUNC = %d, ao_sppt %dmW\n",
		target_func[TYPE_APU_ONLY_SPPT],
		power_limit[target_func[TYPE_APU_ONLY_SPPT]].mwatt[TYPE_APU_ONLY_SPPT]);

	CPRINTF("stt_table = %d\n", (*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER)));
#endif

	if (argc >= 2) {
		if (!strncmp(argv[1], "auto", 4)) {
			manual_ctl = false;
			CPRINTF("Auto Control");
			update_soc_power_limit(false, false);
		}
		if (!strncmp(argv[1], "manual", 6)) {
			manual_ctl = true;
			CPRINTF("Manual Control");
		}

		if (!strncmp(argv[1], "table", 5)) {
			CPRINTF("Table Power Limit:\n");
			for (int i = FUNCTION_DEFAULT; i < FUNCTION_COUNT; i++) {
				CPRINTF("function %d, SPL %dmW, fPPT %dmW, sPPT %dmW, p3T %dmW, ",
					i, power_limit[i].mwatt[TYPE_SPL],
					power_limit[i].mwatt[TYPE_FPPT],
					power_limit[i].mwatt[TYPE_SPPT],
					power_limit[i].mwatt[TYPE_P3T]);
#ifdef CONFIG_BOARD_LOTUS
				CPRINTF("ao_sppt %dmW\n",
					power_limit[i].mwatt[TYPE_APU_ONLY_SPPT]);
#endif
			}
		}

		if (!strncmp(argv[1], "mode", 4)) {
			mode_ctl = strtoi(argv[2], &e, 0);
			CPRINTF("Mode Control");
		}
		if (!strncmp(argv[1], "function", 8)) {
			func_ctl = strtoi(argv[2], &e, 0);
			CPRINTF("func Control");
		}
		if (!strncmp(argv[1], "test_current", 8)) {
			my_test_current = strtoi(argv[2], &e, 0);
			CPRINTF("current Control");
		}
		if (!strncmp(argv[1], "logging", 8)) {
			safety_pwr_logging = strtoi(argv[2], &e, 0);
			CPRINTF("safety logging=%d", safety_pwr_logging);
		}

	}

	if (argc >= 5) {
		spl = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		fppt = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		sppt = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;

		p3t = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM4;

		set_pl_limits(spl, fppt, sppt, p3t);
	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cpupower, cmd_cpupower,
			"cpupower spl fppt sppt p3t (unit mW)",
			"Set/Get the cpupower limit");
