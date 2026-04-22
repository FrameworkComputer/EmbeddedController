#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"

#include "battery.h"
#include "board_function.h"
#include "board_host_command.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "common_cpu_power.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "power.h"
#include "raa489300.h"
#include "task.h"
#include "ucsi.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_emsg.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "throttle_ap.h"
#include "zephyr_console_shim.h"

#ifndef CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
#include "diagnostics_laptop.h"
#endif

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static bool cypd_in_rt_update_mode;

/*****************************************************************
 * Intel Retimer Functions
 ****************************************************************/

void enable_compliance_mode(int controller)
{
	int rv;
	uint32_t debug_register = 0xD0000000;
	int debug_ctl = 0x0100;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	/* Write 0xD0000000 to address 0x0048 */
	rv = cypd_write_reg_block(controller, CCG_ICL_BB_RETIMER_DAT_REG,
			(void *) &debug_register, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_DAT_REG fail");

	/* Write 0x0100 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");
}

void disable_compliance_mode(int controller)
{
	int rv;
	uint32_t debug_register = 0x00000000;
	int debug_ctl = 0x0000;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	/* Write 0x00000000 to address 0x0048 */
	rv = cypd_write_reg_block(controller, CCG_ICL_BB_RETIMER_DAT_REG,
			(void *) &debug_register, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_DAT_REG fail");

	/* Write 0x0000 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");
}

void entry_tbt_mode(int controller)
{
	int rv;
	uint8_t force_tbt_mode = 0x01;
	int debug_ctl = 0x0100;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	/* Write 0x0100 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");

	/* Write 0x01 to address 0x0040 */
	rv = cypd_write_reg8(controller, CCG_ICL_CTRL_REG, force_tbt_mode);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_CTRL_REG fail");
}

void exit_tbt_mode(int controller)
{
	int rv;
	uint8_t force_tbt_mode = 0x00;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

#ifdef CONFIG_PLATFORM_EC_PD_IGNORE_EXIT_RT_MODE
#ifndef CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
	if (get_standalone_mode() && cypd_in_rt_update_mode)
		return;
#endif /* CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC */
#endif /* CONFIG_PLATFORM_EC_PD_IGNORE_EXIT_RT_MODE */

	/* Write 0x00 to address 0x0040 */
	rv = cypd_write_reg8(controller, CCG_ICL_CTRL_REG, force_tbt_mode);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_CTRL_REG fail");
}

int check_tbt_mode(int controller)
{
	int rv;
	int data;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return EC_ERROR_NOT_POWERED;

	rv = cypd_read_reg8(controller, CCG_ICL_STS_REG, &data);
	if (rv != EC_SUCCESS)
		CPRINTS("Read CYP5525_ICL_STS_REG fail");

	return data;
}

static enum ec_status bb_retimer_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_bb_retimer_control_mode *p = args->params;
	struct ec_response_bb_retimer_control_mode *r = args->response;

	r->status = 0;
	args->response_size = sizeof(*r);

	if (p->controller >= PD_CHIP_COUNT)
		return EC_RES_INVALID_PARAM;

	switch (p->modes) {
	case BB_ENTRY_FW_UPDATE_MODE:
		entry_tbt_mode(p->controller);
		cypd_in_rt_update_mode = true;
		break;
	case BB_EXIT_FW_UPDATE_MODE:
		exit_tbt_mode(p->controller);
		break;
	case BB_ENABLE_COMPLIANCE_MODE:
		enable_compliance_mode(p->controller);
		break;
	case BB_DISABLE_COMPLIANCE_MODE:
		disable_compliance_mode(p->controller);
		break;
	case BB_CHECK_STATUS:
		r->status = check_tbt_mode(p->controller);
		args->response_size = sizeof(*r);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BB_RETIMER_CONTROL, bb_retimer_control, EC_VER_MASK(0));
