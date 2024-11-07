#include "board_host_command.h"
#include "chipset.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "console.h"
#include "driver/sb_rmi.h"
#include "hooks.h"
#include "math_util.h"
#include "power.h"
#include "throttle_ap.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

static void update_os_power_slider(int mode)
{
	switch (mode) {
	case EC_AC_BEST_PERFORMANCE:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 140000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 140000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 160000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 400000;
		CPRINTS("AC BEST PERFORMANCE");
		break;
	case EC_AC_BALANCED:
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 100000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 100000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 115000;
		power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 400000;
		CPRINTS("AC BALANCED");
		break;
	default:
		/* no mode, run power table */
		break;
	}
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	static uint32_t old_sustain_power_limit;
	static uint32_t old_fast_ppt_limit;
	static uint32_t old_slow_ppt_limit;
	static uint32_t old_p3t_limit;
	static int old_slider_mode;
	static int set_pl_limit;
	int mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);

	if (!chipset_in_state(CHIPSET_STATE_ON) || !get_apu_ready())
		return;

	if (mode_ctl)
		mode = mode_ctl;

	if (old_slider_mode != mode) {
		old_slider_mode = mode;
		if (func_ctl & 0x1)
			update_os_power_slider(mode);
	}

	if (power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] != old_sustain_power_limit
		|| power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] != old_fast_ppt_limit
		|| power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] != old_slow_ppt_limit
		|| power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] != old_p3t_limit
		|| set_pl_limit || force_update) {
		/* only set PL when it is changed */
		old_sustain_power_limit = power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
		old_slow_ppt_limit = power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
		old_fast_ppt_limit = power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
		old_p3t_limit = power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T];

		CPRINTF("Change SOC Power Limit: SPL %dmW, sPPT %dmW, fPPT %dmW, p3T %dmW\n",
			old_sustain_power_limit, old_slow_ppt_limit,
			old_fast_ppt_limit, old_p3t_limit);
		set_pl_limit = set_pl_limits(old_sustain_power_limit, old_fast_ppt_limit,
			old_slow_ppt_limit, old_p3t_limit);
	}
}

static void initial_soc_power_limit(void)
{
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL] = 100000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT] = 100000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT] = 115000;
	power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T] = 400000;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, initial_soc_power_limit, HOOK_PRIO_DEFAULT);
