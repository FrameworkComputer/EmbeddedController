#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"

#include "battery.h"
#include "board_function.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "common_cpu_power.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
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


__override int cypd_write_reg8_wait_ack(int controller, int reg, int data)
{
	int rv = EC_SUCCESS;
	int intr_status;
	int event;
	int cmd_port = -1;
	int ack_mask = 0;
	int expected_ack_mask = 0;
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);

	if (reg < 0x1000) {
		expected_ack_mask = CCG_DEV_INTR;
		cmd_port = -1;
	} else if (reg < 0x2000) {
		expected_ack_mask = CCG_PORT0_INTR;
		cmd_port = 0;
	} else {
		expected_ack_mask = CCG_PORT1_INTR;
		cmd_port = 1;
	}

	if (gpio_pin_get_dt(intr) == 0) {
		/* we may have a pending interrupt */
		rv = cypd_get_int(controller, &intr_status);
		CPRINTS("%s pre 0x%x ", __func__, intr_status);
		if (intr_status & CCG_DEV_INTR) {
			rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
			if (event < 0x80) {
				cypd_clear_int(controller, CCG_DEV_INTR);
			}
			usleep(50);
		}
	}


	rv = cypd_write_reg8(controller, reg, data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write Reg8 0x%x fail!", reg);

	if (cypd_wait_for_ack(controller, 100) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	rv = cypd_get_int(controller, &intr_status);
	if (rv != EC_SUCCESS)
		CPRINTS("Get INT Fail");

	if (intr_status & CCG_DEV_INTR && cmd_port == -1) {
		if (data == CCG6_AC_AT_PORT) {
            rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
            if (rv != EC_SUCCESS)
                CPRINTS("fail to read DEV response");
			switch (event) {
			case CCG6_RESPONSE_AC_AT_P0:
				pd_port_states[(controller * 2) + 0].ac_port = 1;
				break;
			case CCG6_RESPONSE_AC_AT_P1:
				pd_port_states[(controller * 2) + 1].ac_port = 1;
				break;
			case CCG6_RESPONSE_NO_AC:
			case CCG6_RESPONSE_EC_MODE:
				break;
			default:
				CPRINTS("Check AC get unknown event 0x%04x", event);
			}
		}
		ack_mask = CCG_DEV_INTR;
	} else if (intr_status & CCG_PORT0_INTR && cmd_port == 0) {
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(0), &event);
		if (rv != EC_SUCCESS)
			CPRINTS("fail to read P0 response");
		ack_mask = CCG_PORT0_INTR;
	} else if (intr_status & CCG_PORT1_INTR && cmd_port == 1) {
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(1), &event);
		if (rv != EC_SUCCESS)
			CPRINTS("fail to read P1 response");
		ack_mask = CCG_PORT1_INTR;
	} else {
		CPRINTS("%s C:%d Unexpected response 0x%x to reg 0x%x",
			__func__, controller, intr_status, reg);
		rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
		CPRINTS("Dev 0x%x", event);
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(0), &event);
		CPRINTS("P0 0x%x", event);
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(1), &event);
		CPRINTS("P1 0x%x", event);
	}

	/* only clear response code let main task handle event code */
	if (event < 0x80) {
		cypd_clear_int(controller, ack_mask);
		if (event != CCG_RESPONSE_SUCCESS) {
			CPRINTS("%s C:%d 0x%x response 0x%x",
				__func__, controller, reg, event);
		}
		rv = (event == CCG_RESPONSE_SUCCESS) ? EC_SUCCESS : EC_ERROR_INVAL;
	}

	usleep(50);
	return rv;
}