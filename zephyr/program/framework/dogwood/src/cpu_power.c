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
		break;
	}
}

static int update_soc_vrm_current_limit(void)
{
	/* Follow the AMD specification to set the maximum value (unit:mA) */

	RETURN_ERROR(update_vrm_vdd_current_limit(160000));
	RETURN_ERROR(update_vrm_vdd_max_current_limit(224000));
	RETURN_ERROR(update_vrm_vdd_ccd_current_limit(80000));
	RETURN_ERROR(update_vrm_vdd_ccd_max_current_limit(125000));
	RETURN_ERROR(update_vrm_soc_current_limit(40000));
	RETURN_ERROR(update_vrm_soc_max_current_limit(55000));
	return EC_SUCCESS;
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	int slider_mode = *host_get_memmap(EC_MEMMAP_POWER_SLIDE);
	static int old_slider_mode;
	static bool soc_pmf_has_updated, soc_current_limit_has_updated;

	/* Needs to update the PMF and curent limit when the system power on*/
	if (!chipset_in_state(CHIPSET_STATE_ON) || !get_apu_ready()) {
		soc_current_limit_has_updated = false;
		soc_pmf_has_updated = false;
		old_slider_mode = EC_AC_BALANCED;
		return;
	}

	/* System power mode change, update the PMF */
	if (slider_mode != old_slider_mode && slider_mode != 0) {
		update_os_power_slider(slider_mode);
		old_slider_mode = slider_mode;
		soc_pmf_has_updated = false;
	}

	/* force update the SoC PMF and current limit */
	if (force_update) {
		soc_current_limit_has_updated = false;
		soc_pmf_has_updated = false;
	}

	if (!soc_pmf_has_updated) {
		uint32_t soc_pmf_spl, soc_pmf_sppt, soc_pmf_fppt, soc_pmf_p3t;

		soc_pmf_spl = power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPL];
		soc_pmf_sppt = power_limit[FUNCTION_SLIDER].mwatt[TYPE_SPPT];
		soc_pmf_fppt = power_limit[FUNCTION_SLIDER].mwatt[TYPE_FPPT];
		soc_pmf_p3t = power_limit[FUNCTION_SLIDER].mwatt[TYPE_P3T];

		if (set_pl_limits(soc_pmf_spl, soc_pmf_sppt,
				soc_pmf_fppt, soc_pmf_p3t) == EC_SUCCESS) {
			CPRINTS("Update SoC PFM: SPL %dmW, sPPT %dmW, fPPT %dmW, p3T %dmW",
			soc_pmf_spl, soc_pmf_sppt, soc_pmf_fppt, soc_pmf_p3t);
			soc_pmf_has_updated = true;
		} else
			return;
	}

	if (!soc_current_limit_has_updated) {
		if (update_soc_vrm_current_limit() == EC_SUCCESS) {
			CPRINTS("Update soc vrm current limit success");
			soc_current_limit_has_updated = true;
		}
	}
}

static void initial_soc_power_limit(void)
{
	update_os_power_slider(EC_AC_BALANCED);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, initial_soc_power_limit, HOOK_PRIO_DEFAULT);
