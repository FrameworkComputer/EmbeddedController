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

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static int cypd_enable_retimer_update_mode(int controller, uint8_t data)
{
	int rv;

	/* Wrong controller index */
	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	/**
	 * On the AMD platform, EC needs to send the HPI vendor specific command
	 * 0x0043 data 0x01 to enable retimer update mode.
	 * 0x0043 data 0x00 to disable retimer update mode.
	 */
	rv = cypd_write_reg8(controller, CCG_RETIMER_UPDATE_MODE_CONTROL, data);

	if (rv != EC_SUCCESS)
		CPRINTS("CCG8 Retimer update mode control failed:%d", rv);

	return rv;
}

static enum ec_status bb_retimer_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_bb_retimer_control_mode *p = args->params;
	int rv;

	switch (p->modes) {
	case BB_ENTRY_FW_UPDATE_MODE:
		rv = cypd_enable_retimer_update_mode(p->controller, 0x01);
		if (rv != EC_SUCCESS) {
			if (rv == EC_ERROR_PARAM1)
				return EC_RES_INVALID_PARAM;
			else
				return EC_RES_BUS_ERROR;
		}
		break;
	case BB_EXIT_FW_UPDATE_MODE:
		rv = cypd_enable_retimer_update_mode(p->controller, 0x00);
		if (rv != EC_SUCCESS) {
			if (rv == EC_ERROR_PARAM1)
				return EC_RES_INVALID_PARAM;
			else
				return EC_RES_BUS_ERROR;
		}
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BB_RETIMER_CONTROL, bb_retimer_control, EC_VER_MASK(0));
