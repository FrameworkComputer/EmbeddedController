/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

#ifdef CONFIG_BOARD_LOTUS
#include "gpu.h"
#endif

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#undef CCG_INIT_STATE
#ifdef CONFIG_PD_CHIP_CCG6
#define CCG_INIT_STATE CCG_STATE_WAIT_STABLE
#else
#define CCG_INIT_STATE CCG_STATE_POWER_ON
#endif


/*
 * Unimplemented functions:
 * 1. Control port current 3A/1.5A for GRL test.
 * 2. Control port VBUS enable/disable.
 * 3. Update system power state to PD chip. (Avoid PD chip does the error recovery)
 * 4. Control PD chip compliance mode
 * 5. Flash PD flow
 * 6. Extended message handler
 * 7. UCSI handler
 */

struct pd_chip_config_t pd_chip_config[] = {
	[PD_CHIP_0] = {
		.i2c_port = I2C_PORT_PD_MCU0,
		.addr_flags = CCG_I2C_CHIP0 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CCG_INIT_STATE,
		.gpio = GPIO_EC_PD_INTA_L,
	},
	[PD_CHIP_1] = {
		.i2c_port = I2C_PORT_PD_MCU1,
		.addr_flags = CCG_I2C_CHIP1 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CCG_INIT_STATE,
		.gpio = GPIO_EC_PD_INTB_L,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pd_chip_config) == PD_CHIP_COUNT);

struct pd_port_current_state_t pd_port_states[] = {
	[PD_PORT_0] = {

	},
	[PD_PORT_1] = {

	},
	[PD_PORT_2] = {

	},
	[PD_PORT_3] = {

	}
};

struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

static int prev_charge_port = -1;
static bool verbose_msg_logging;
static bool firmware_update;

/*****************************************************************************/
/* Internal functions */

static void cypd_pdo_reset_deferred(void);
static void cypd_set_prepare_pdo(int controller, int port);

int cypd_write_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_write_offset16_block(i2c_port, addr_flags, reg, data, len);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_write_reg16(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 2);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_write_reg8(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, reg, data, len);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg16(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 2);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg8(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

static int cypd_reset(int controller)
{
	/*
	 * Device Reset: This command is used to request the CCG device to perform a soft reset
	 * and start at the boot-loader stage again
	 * Note: need barrel AC or battery
	 */
	return cypd_write_reg16(controller, CCG_RESET_REG, CCG_RESET_CMD);
}

int cypd_get_int(int controller, int *intreg)
{
	int rv;

	rv = cypd_read_reg8(controller, CCG_INTR_REG, intreg);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, rv=0x%02x", __func__, controller, rv);
	return rv;
}

int cypd_clear_int(int controller, int mask)
{
	int rv;

	rv = cypd_write_reg8(controller, CCG_INTR_REG, mask);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, mask=0x%02x", __func__, controller, mask);
	return rv;
}

int cypd_wait_for_ack(int controller, int timeout_ms)
{
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);
	timestamp_t start = get_time();

	/* wait for interrupt ack to be asserted */
	do {
		if (gpio_pin_get_dt(intr) == 0)
			break;
		usleep(100);
	} while (time_since32(start) < (timeout_ms * MSEC));

	/* make sure response is ok */
	if (gpio_pin_get_dt(intr) != 0) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

void cypd_print_buff(const char *msg, void *buff, int len)
{
	int i;
	uint8_t *data = (uint8_t *)buff;

	CPRINTF("%s 0x", msg);
	for (i = len-1; i >= 0; i--) {
		CPRINTF("%02x", data[i]);
	}
	CPRINTF("\n");
}

#ifdef CONFIG_BOARD_LOTUS
static void update_external_cc_mux(int port, int cc)
{
	if (port == 1) {
		switch(cc) {
			case POLARITY_CC1:
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc1), 1);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc2), 0);
				break;

			case POLARITY_CC2:
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc1), 0);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc2), 1);
				break;

			default:
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc1), 0);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc2), 0);
		}
	}
}
#endif

static void pd0_update_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0);
}
DECLARE_DEFERRED(pd0_update_state_deferred);

static void pd1_update_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_1);

}
DECLARE_DEFERRED(pd1_update_state_deferred);

void update_power_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_UPDATE_PWRSTAT);
}

static void cypd_enable_interrupt(int controller, int enable_ndisable)
{
	if (controller) {
			if (enable_ndisable)
				gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
			else
				gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
	}else {
			if (enable_ndisable)
				gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
			else
				gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
	}
}

static void cypd_print_version(int controller, const char *vtype, uint8_t *data)
{
	/*
	 * Base version: Cypress release version
	 * Application version: FAE release version
	 */
	CPRINTS("Controller %d  %s version B:%X.%X.%X.%X , AP:%X.%X.%X",
		controller, vtype,
		(data[3]>>4) & 0xF, (data[3]) & 0xF, data[2], data[0] + (data[1]<<8),
		(data[7]>>4) & 0xF, (data[7]) & 0xF, data[6]);
}

static void cypd_get_version(int controller)
{
	int rv;
	int i;
	uint8_t data[24];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, CCG_READ_ALL_VERSION_REG, data, 24);
	if (rv != EC_SUCCESS)
		CPRINTS("READ_ALL_VERSION_REG failed");

	cypd_print_version(controller, "App1", data+8);
	cypd_print_version(controller, "App2", data+16);

	/* store the FW2 version into pd_chip_info struct */
	for (i = 0; i < 8; i++)
		pd_chip_config[controller].version[i] = data[16+i];
}

static void pdo_init_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_INIT_0);
}
DECLARE_DEFERRED(pdo_init_deferred);

static void cypd_pdo_init(int controller, int port, uint8_t profile)
{
	int rv;

	/*
	 * EC needs to provide the data for all Source PDOs when doing a dynamic update of the PDOs.
	 * If less than 7 PDOs are required, the remaining PDO values should be set to 0.
	 */
	uint8_t pdos_reg[32] = {
			0x50, 0x43, 0x52, 0x53,	/* “SRCP”		*/
			0x96, 0x90, 0x01, 0x27,	/* PDO0 - 1.5A	*/
			0x2c, 0x91, 0x01, 0x27,	/* PDO1 - 3A	*/
			0x00, 0x00, 0x00, 0x00,	/* PDO2			*/
			0x00, 0x00, 0x00, 0x00,	/* PDO3			*/
			0x00, 0x00, 0x00, 0x00,	/* PDO4			*/
			0x00, 0x00, 0x00, 0x00,	/* PDO5			*/
			0x00, 0x00, 0x00, 0x00	/* PDO6			*/
		};

	rv = cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 0),
		pdos_reg, sizeof(pdos_reg));
	if (rv != EC_SUCCESS)
		CPRINTS("SET CCG_MEMORY failed");

	rv = cypd_write_reg8_wait_ack(controller, CCG_SELECT_SOURCE_PDO_REG(port), profile);
	if (rv != EC_SUCCESS)
		CPRINTS("SET CCG_SELECT_REG failed");

	memset(pdos_reg, 0, sizeof(pdos_reg));

	/* Clear Signature “SRCP” for PDO update finish */
	rv = cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 0),
		pdos_reg, sizeof(pdos_reg));
	if (rv != EC_SUCCESS)
		CPRINTS("CLEAR CCG_MEMORY failed");
}

static int cypd_select_rp(int port, uint8_t profile)
{
	int rv;
	CPRINTF("P:%d SET TYPEC RP=%d", port, profile);

	rv = cypd_write_reg8_wait_ack(PORT_TO_CONTROLLER(port),
			CCG_PD_CONTROL_REG(PORT_TO_CONTROLLER_PORT(port)),
			profile);
	if (rv != EC_SUCCESS)
		CPRINTS("SET TYPEC RP failed");

	return rv;
}

static int cypd_select_pdo(int controller, int port, uint8_t profile)
{
	int rv;

	rv = cypd_write_reg8_wait_ack(controller, CCG_SELECT_SOURCE_PDO_REG(port), profile);
	if (rv != EC_SUCCESS)
		CPRINTS("SET CCG_SELECT_REG failed");

	return rv;
}

static int pd_3a_flag;
static int pd_3a_set;
static int pd_3a_controller;
static int pd_3a_port;
static int first_3a_controller;
static int first_3a_port;
static int first_port_idx;
static int pd_ports_1_5A_flag[PD_PORT_COUNT];
static int rdo_3a_flag[PD_PORT_COUNT];

int cypd_port_3a_status(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	if (pd_3a_flag &&
		controller == pd_3a_controller &&
		port_idx == pd_3a_port)
		return true;
	return false;
}

int cypd_port_3a_set(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	if (pd_3a_set)
		return false;

	pd_3a_set = 1;
	pd_3a_flag = 1;
	pd_3a_controller = controller;
	pd_3a_port = port_idx;

	return true;
}

void cypd_port_3a_change(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	pd_3a_set = 1;
	pd_3a_flag = 1;
	pd_3a_controller = controller;
	pd_3a_port = port_idx;
}

void cypd_port_1_5a_set(int controller, int port)
{
	int port_idx = (controller << 1) + port;
	pd_ports_1_5A_flag[port_idx] = 1;
}

int cypd_port_force_3A(int controller, int port)
{
	int port_idx = (controller << 1) + port;
	int port_1_5A_idx = 0;
	int i;
	for (i = 0; i < PD_PORT_COUNT; i++) {
		port_1_5A_idx += pd_ports_1_5A_flag[i];
	}

	if (port_1_5A_idx >= 3) {
		if (!pd_ports_1_5A_flag[port_idx])
			return true;
	}
	return false;
}

void cypd_release_port(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	/* if port disconnect should set RP and PDO to default */

	cypd_select_rp(port_idx, CCG_PD_CMD_SET_TYPEC_1_5A);
	cypd_select_pdo(controller, port, CCG_PD_CMD_SET_TYPEC_3A);

	if (cypd_port_3a_status(controller, port)) {
		pd_3a_set = 0;
		pd_3a_flag = 0;
	}
	pd_ports_1_5A_flag[port_idx] = 0;
	rdo_3a_flag[port_idx] = 0;
}

void cypd_clear_port(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	if (cypd_port_3a_status(controller, port)) {
		pd_3a_set = 0;
		pd_3a_flag = 0;
	}
	pd_ports_1_5A_flag[port_idx] = 0;
	rdo_3a_flag[port_idx] = 0;
}

/*
 * function for profile check, if profile not change
 * don't send again.
 */
int cypd_profile_check(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	return pd_ports_1_5A_flag[port_idx] != 0;
}

static void pdo_c0p0_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C0P0);
}
DECLARE_DEFERRED(pdo_c0p0_deferred);

static void pdo_c0p1_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C0P1);
}
DECLARE_DEFERRED(pdo_c0p1_deferred);

static void pdo_c1p0_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C1P0);
}
DECLARE_DEFERRED(pdo_c1p0_deferred);

static void pdo_c1p1_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C1P1);
}
DECLARE_DEFERRED(pdo_c1p1_deferred);

static void cypd_set_prepare_pdo(int controller, int port)
{
	switch (controller) {
	case 0:
		if (!port)
			hook_call_deferred(&pdo_c0p0_deferred_data, 2000 * MSEC);
		else
			hook_call_deferred(&pdo_c0p1_deferred_data, 2100 * MSEC);
		break;
	case 1:
		if (!port)
			hook_call_deferred(&pdo_c1p0_deferred_data, 2000 * MSEC);
		else
			hook_call_deferred(&pdo_c1p1_deferred_data, 2100 * MSEC);
		break;
	}
}

static int cypd_modify_profile(int controller, int port, int profile)
{
	int rv;
	int port_idx = (controller << 1) + port;

	if (verbose_msg_logging)
		CPRINTS("PD Select PDO %s ", profile & 0x02 ? "3A" : "1.5A");

	if (profile == CCG_PD_CMD_SET_TYPEC_3A) {
		rv = cypd_select_rp(port_idx, profile);
		if (rv != EC_SUCCESS)
			return rv;
	}

	rv = cypd_select_pdo(controller, port, profile);
	if (rv != EC_SUCCESS) {
		CPRINTS("PD Select PDO %s failed", profile & 0x02 ? "3A" : "1.5A");
		cypd_clear_port(controller, port);
		cypd_set_prepare_pdo(controller, port);
		return rv;
	}

	/* Lock 1.5A port  */
	if (profile == CCG_PD_CMD_SET_TYPEC_1_5A)
		cypd_port_1_5a_set(controller, port);

	return EC_SUCCESS;
}

int cypd_modify_safety_power(int controller, int port, int profile)
{
	int rv;
	int port_idx = (controller << 1) + port;

	if (verbose_msg_logging)
		CPRINTS("PD Select PDO %s ", profile & 0x02 ? "3A" : "1.5A");

	rv = cypd_select_rp(port_idx, profile);
	rv = cypd_select_pdo(controller, port, profile);
	if (rv != EC_SUCCESS) {
		CPRINTS("PD Select PDO %s failed", profile & 0x02 ? "3A" : "1.5A");
		cypd_clear_port(controller, port);
		cypd_set_prepare_pdo(controller, port);
		return rv;
	}

	return EC_SUCCESS;
}

void cypd_set_typec_profile(int controller, int port)
{
	int rv;
	uint8_t pd_status_reg[4];
	uint8_t rdo_reg[4];

	int rdo_max_current = 0;
	int rdo_3a_idx = 0;
	int port_idx = (controller << 1) + port;

	rv = cypd_read_reg_block(controller, CCG_PD_STATUS_REG(port), pd_status_reg, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_PD_STATUS_REG failed");

	/*do we have a valid PD contract*/
	pd_port_states[port_idx].pd_state = pd_status_reg[1] & BIT(2) ? 1 : 0;
	pd_port_states[port_idx].power_role =
			pd_status_reg[1] & BIT(0) ? PD_ROLE_SOURCE : PD_ROLE_SINK;

	if (pd_port_states[port_idx].power_role == PD_ROLE_SOURCE) {
		if (pd_port_states[port_idx].pd_state) {
			/*
			 * first time set 3A PDO to device
			 * when device request RDO <= 1.5A
			 * resend 1.5A pdo to device
			 */

			cypd_read_reg_block(controller, CCG_CURRENT_RDO_REG(port), rdo_reg, 4);
			rdo_max_current = (((rdo_reg[1]>>2) + (rdo_reg[2]<<6)) & 0x3FF)*10;
			if (rdo_max_current > 1500) {
				rdo_3a_flag[port_idx] = 1;
				for (int i = 0; i < PD_PORT_COUNT; i++) {
					rdo_3a_idx += rdo_3a_flag[i];
				}
			}

			/* The first device force 3A PDO */
			if (!pd_3a_flag && cypd_port_3a_set(controller, port)) {
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_3A);
				first_3a_controller = controller;
				first_3a_port = port;
				first_port_idx = (controller << 1) + port;
			/* Another device requires 3A, and the first device can drop to 1.5A */
			} else if (rdo_3a_flag[port_idx] && rdo_3a_idx == 1) {
				if (first_port_idx == port_idx)
					return;
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_3A);
				cypd_select_rp(first_port_idx, CCG_PD_CMD_SET_TYPEC_1_5A);
				rv = cypd_modify_profile(first_3a_controller, first_3a_port,
						CCG_PD_CMD_SET_TYPEC_1_5A);
				cypd_port_3a_change(controller, port);
			} else if ((cypd_port_force_3A(controller, port) && !pd_3a_flag) ||
				cypd_port_3a_status(controller, port)) {
				if (!cypd_port_3a_set(controller, port))
					return;
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_3A);
			} else if (!cypd_port_3a_status(controller, port))
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_1_5A);
		} else {
			cypd_write_reg8(controller, CCG_PD_CONTROL_REG(port),
				CCG_PD_CMD_SET_TYPEC_1_5A);
		}
	}

	cypd_update_port_state(controller, port);
}

void cypd_port_current_setting(void)
{
	for (int i = 0; i < PD_CHIP_COUNT; i++) {
		cypd_set_prepare_pdo(i, 0);
		cypd_set_prepare_pdo(i, 1);
	}
}

static void cypd_pdo_reset_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_RESET);
}
DECLARE_DEFERRED(cypd_pdo_reset_deferred);

static void cypd_ppm_port_clear(void)
{
	memset(pd_ports_1_5A_flag, 0, sizeof(pd_ports_1_5A_flag));
	pd_3a_set = 0;

	/* need init PDO again because PD chip will clear PDO data */
	hook_call_deferred(&pdo_init_deferred_data, 1);
}

#ifdef CONFIG_PD_COMMON_EXTENDED_MESSAGE
/*
 * send a message using DM_CONTROL to port partner
 * pd_header is using chromium PD header with upper bits defining SOP type
 * pd30 is set for batttery status messages
 * response timer is set to false for messages that are a response
 * data includes
 * pd header bytes 0 -1
 * message, or extmessage header - then data
 * length should include length of all data after pd header
 */
void cypd_send_msg(int controller, int port, uint32_t pd_header, uint16_t ext_hdr,
	bool pd30, bool response_timer, void *data, uint32_t data_size)
{
	uint16_t header[2] = {0};
	uint16_t dm_control_data;

	/**
	 * The extended message data should be written to the write data memory
	 * in the following format:
	 * Byte 0 : Message type [4:0]
	 * Byte 1 : Reserved
	 * Byte 3 - 2 : Extended message header
	 * Byte N - 4 : data
	 */

	header[0] = pd_header;
	header[1] = ext_hdr;

	cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 0),
		(void *)header, 4);

	cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 4),
		data, data_size);

	/**
	 * The DM_CONTROL register should then be written to in the following format:
	 * Byte 0
	 *	- BIT 1 - 0 : Packet type should be set to SOP(0), SOP'(1), or SOP''(2).
	 *	- BIT 2 : PD 3.0 Message bit (Bit 2) should be clear.
	 *	- BIT 3 : Extended message bit (Bit 3) should be set.
	 *	- BIT 4 : Respoonse timer disable bit should be set as desired.
	 * Byte 1 : The data length specified here will be the actual length of data
	 *			written into the write data memory, inclusive of the 4 byte header
	 *
	 * TODO: Need to process chunk extended message [4:32]
	 */
	dm_control_data = PD_HEADER_GET_SOP(pd_header);
	if (ext_hdr)
		dm_control_data |= CCG_DM_CTRL_EXTENDED_DATA_REQUEST;
	if (pd30)
		dm_control_data |= CCG_DM_CTRL_PD3_DATA_REQUEST;
	if (!response_timer)
		dm_control_data |= CCG_DM_CTRL_SENDER_RESPONSE_TIMER_DISABLE;
	dm_control_data += ((data_size + 4) << 8);

	cypd_write_reg16(controller, CCG_DM_CONTROL_REG(port), dm_control_data);
}


void cypd_response_get_battery_capability(int controller, int port,
	uint32_t pd_header, enum tcpci_msg_type sop_type)
{
	int port_idx = (controller << 1) + port;
	int ext_header = 0;
	bool chunked = PD_EXT_HEADER_CHUNKED(rx_emsg[port_idx].header);
	uint16_t msg[5] = {0, 0, 0, 0, 0};
	uint32_t header = PD_EXT_BATTERY_CAP + PD_HEADER_SOP(sop_type);

	ext_header = 9;
	/* Set extended header */
	if (chunked) {
		ext_header |= BIT(15);
	}
	/* Set VID */
	msg[0] = VENDOR_ID;

	/* Set PID */
	msg[1] = PRODUCT_ID;

	if (battery_is_present() == BP_YES) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (rx_emsg[port_idx].buf[0] != 0) {
			/* Invalid battery reference */
			msg[4] = 1;
		} else {
			uint32_t v;
			uint32_t c;

			/*
			 * The Battery Design Capacity field shall return the
			 * Battery’s design capacity in tenths of Wh. If the
			 * Battery is Hot Swappable and is not present, the
			 * Battery Design Capacity field shall be set to 0. If
			 * the Battery is unable to report its Design Capacity,
			 * it shall return 0xFFFF
			 */
			msg[2] = 0xffff;

			/*
			 * The Battery Last Full Charge Capacity field shall
			 * return the Battery’s last full charge capacity in
			 * tenths of Wh. If the Battery is Hot Swappable and
			 * is not present, the Battery Last Full Charge Capacity
			 * field shall be set to 0. If the Battery is unable to
			 * report its Design Capacity, the Battery Last Full
			 * Charge Capacity field shall be set to 0xFFFF.
			 */
			msg[3] = 0xffff;

			if (battery_design_voltage(&v) == 0) {
				if (battery_design_capacity(&c) == 0) {
					/*
					 * Wh = (c * v) / 1000000
					 * 10th of a Wh = Wh * 10
					 */
					msg[2] = DIV_ROUND_NEAREST((c * v),
								100000);
				}

				if (battery_full_charge_capacity(&c) == 0) {
					/*
					 * Wh = (c * v) / 1000000
					 * 10th of a Wh = Wh * 10
					 */
					msg[3] = DIV_ROUND_NEAREST((c * v),
								100000);
				}
			}
		}
	}
	cypd_send_msg(controller, port, header, ext_header,  false, false,
		(void *)msg, ARRAY_SIZE(msg)*sizeof(uint16_t));

}

int cypd_response_get_battery_status(int controller, int port,
	uint32_t pd_header, enum tcpci_msg_type sop_type)
{
	int rv = 0;
	uint32_t msg = 0;
	uint32_t header = PD_DATA_BATTERY_STATUS + PD_HEADER_SOP(sop_type);
	int port_idx = (controller << 1) + port;

	if (battery_is_present() == BP_YES) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (rx_emsg[port_idx].buf[0] != 0) {
			/* Invalid battery reference */
			msg |= BSDO_INVALID;
		} else {
			uint32_t v;
			uint32_t c;

			if (battery_design_voltage(&v) != 0 ||
					battery_remaining_capacity(&c) != 0) {
				msg |= BSDO_CAP(BSDO_CAP_UNKNOWN);
			} else {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				msg |= BSDO_CAP(DIV_ROUND_NEAREST((c * v),
								100000));
			}

			/* Battery is present */
			msg |= BSDO_PRESENT;

			/*
			 * For drivers that are not smart battery compliant,
			 * battery_status() returns EC_ERROR_UNIMPLEMENTED and
			 * the battery is assumed to be idle.
			 */
			if (battery_status(&c) != 0) {
				msg |= BSDO_IDLE; /* assume idle */
			} else {
				if (c & STATUS_FULLY_CHARGED)
					/* Fully charged */
					msg |= BSDO_IDLE;
				else if (c & STATUS_DISCHARGING)
					/* Discharging */
					msg |= BSDO_DISCHARGING;
				/* else battery is charging.*/
			}
		}
	} else {
		msg = BSDO_CAP(BSDO_CAP_UNKNOWN);
	}

	cypd_send_msg(controller, port, header, 0,  true, false, &msg, 4);

	return rv;
}

int cypd_handle_extend_msg(int controller, int port, int len, enum tcpci_msg_type sop_type)
{
	/**
	 * Extended Message Received Events
	 * Event Code = 0xAC(SOP), 0xB4(SOP'), 0xB5(SOP'')
	 * Event length = 4 + Extended message length
	 */

	/*Todo handle full length Extended messages up to 260 bytes*/
	int type;
	int rv;
	int i;
	int port_idx = (controller << 1) + port;
	int pd_header;

	if (len > 260) {
		CPRINTS("ExtMsg Too Long");
		return EC_ERROR_INVAL;
	}

	/* Read the extended message packet */
	rv = cypd_read_reg_block(controller,
		CCG_READ_DATA_MEMORY_REG(port, 0), (void *)&(rx_emsg[port_idx].len), len);
		/*
		 * avoid a memcopy so direct copy into the buffer and then swap header and len
		 * look at the memory layout for the rx_emsg structure to see why we do this
		 */
	rx_emsg[port_idx].header = rx_emsg[port_idx].len >> 16;
	pd_header = (rx_emsg[port_idx].len & 0xFFFF) + PD_HEADER_SOP(sop_type);
	rx_emsg[port_idx].len = len-4;

	/* Extended field shall be set to 1*/
	if (!PD_HEADER_EXT(pd_header))
		return EC_ERROR_INVAL;

	type = PD_HEADER_TYPE(pd_header);

	switch (type) {
	case PD_EXT_GET_BATTERY_CAP:
		cypd_response_get_battery_capability(controller, port, pd_header, sop_type);
		break;
	case PD_EXT_GET_BATTERY_STATUS:
		rv = cypd_response_get_battery_status(controller, port, pd_header, sop_type);
		break;
	default:
		CPRINTF("Port:%d Unknown data type: 0x%02x Hdr:0x%04x ExtHdr:0x%04x Data:0x",
				port_idx, type, pd_header, rx_emsg[port_idx].header);
		for (i = 0; i < rx_emsg[port_idx].len; i++) {
			CPRINTF("%02x", rx_emsg[port_idx].buf[i]);
		}
		CPRINTF("\n");
		rv = EC_ERROR_INVAL;
		break;
	}

	return rv;
}
#endif

static void clear_port_state(int controller, int port)
{
	int port_idx = (controller << 1) + port;
	pd_port_states[port_idx].pd_state = 0; /*do we have a valid PD contract*/
	pd_port_states[port_idx].power_role = PD_ROLE_SINK;
	pd_port_states[port_idx].data_role = PD_ROLE_UFP;
	pd_port_states[port_idx].vconn = PD_ROLE_VCONN_OFF;
	pd_port_states[port_idx].epr_active = 0;
	pd_port_states[port_idx].epr_support = 0;
	pd_port_states[port_idx].cc = POLARITY_CC1;
	pd_port_states[port_idx].c_state = 0;
	pd_port_states[port_idx].current = 0;
	pd_port_states[port_idx].voltage = 0;
}

void cypd_update_port_state(int controller, int port)
{
	int rv;
	uint8_t pd_status_reg[4];
	uint32_t pdo_reg;
	uint8_t rdo_reg[4];
	int typec_status_reg;
	int pd_current = 0;
	int pd_voltage = 0;
	int rdo_max_current = 0;
	int type_c_current = 0;
	int port_idx = (controller << 1) + port;
#ifdef CONFIG_PD_CCG8_EPR
	int64_t calculate_ma;
#endif

	rv = cypd_read_reg_block(controller, CCG_PD_STATUS_REG(port), pd_status_reg, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("CCG_PD_STATUS_REG failed");
	pd_port_states[port_idx].pd_state =
		pd_status_reg[1] & BIT(2) ? 1 : 0; /*do we have a valid PD contract*/
	pd_port_states[port_idx].power_role =
		pd_status_reg[1] & BIT(0) ? PD_ROLE_SOURCE : PD_ROLE_SINK;
	pd_port_states[port_idx].data_role =
		pd_status_reg[0] & BIT(6) ? PD_ROLE_DFP : PD_ROLE_UFP;
	pd_port_states[port_idx].vconn =
		pd_status_reg[1] & BIT(5) ? PD_ROLE_VCONN_SRC : PD_ROLE_VCONN_OFF;
	if (pd_port_states[port_idx].epr_active != 0xff)
		pd_port_states[port_idx].epr_active = pd_status_reg[2] & BIT(7) ? 1 : 0;

	rv = cypd_read_reg8(controller, CCG_TYPE_C_STATUS_REG(port), &typec_status_reg);
	if (rv != EC_SUCCESS)
		CPRINTS("CCG_TYPE_C_STATUS_REG failed");

	pd_port_states[port_idx].cc = typec_status_reg & BIT(1) ? POLARITY_CC2 : POLARITY_CC1;
	pd_port_states[port_idx].c_state = (typec_status_reg >> 2) & 0x7;
	switch ((typec_status_reg >> 6) & 0x03) {
	case 0:
		type_c_current = 900;
		break;
	case 1:
		type_c_current = 1500;
		break;
	case 2:
		type_c_current = 3000;
		break;
	}
#ifdef CONFIG_BOARD_LOTUS
	update_external_cc_mux(port_idx,pd_port_states[port_idx].c_state == CCG_STATUS_NOTHING ? 0xFF : pd_port_states[port_idx].cc);
#endif

	rv = cypd_read_reg_block(controller, CCG_CURRENT_PDO_REG(port), &pdo_reg, 4);
	switch (pdo_reg & PDO_TYPE_MASK) {
		case PDO_TYPE_FIXED:
			pd_current = PDO_FIXED_CURRENT(pdo_reg);
			pd_voltage = PDO_FIXED_VOLTAGE(pdo_reg);
			break;
		case PDO_TYPE_BATTERY:
			pd_current = PDO_BATT_MAX_POWER(pdo_reg)/PDO_BATT_MAX_VOLTAGE(pdo_reg);
			pd_voltage = PDO_BATT_MIN_VOLTAGE(pdo_reg);
		break;
		case PDO_TYPE_VARIABLE:
			pd_current = PDO_VAR_MAX_CURRENT(pdo_reg);
			pd_voltage = PDO_VAR_MAX_VOLTAGE(pdo_reg);
		break;
		case PDO_TYPE_AUGMENTED:
			pd_current = PDO_AUG_MAX_CURRENT(pdo_reg);
			pd_voltage = PDO_AUG_MAX_VOLTAGE(pdo_reg);
		break;
	}

	cypd_read_reg_block(controller, CCG_CURRENT_RDO_REG(port), rdo_reg, 4);
	rdo_max_current = (((rdo_reg[1]>>2) + (rdo_reg[2]<<6)) & 0x3FF)*10;

	/*
	 * The port can have several states active:
	 * 1. Type C active (with no PD contract) CC resistor negociation only
	 * 2. Type C active with PD contract
	 * 3. Not active
	 * Each of 1 and 2 can be either source or sink
	 */

	if (pd_port_states[port_idx].c_state == CCG_STATUS_SOURCE) {
		typec_set_input_current_limit(port_idx, type_c_current, TYPE_C_VOLTAGE);
		charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD,
							type_c_current);
		pd_port_states[port_idx].current = type_c_current;
		pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;
	} else {
		typec_set_input_current_limit(port_idx, 0, 0);
		charge_manager_set_ceil(port,
			CEIL_REQUESTOR_PD,
			CHARGE_CEIL_NONE);
	}
	if (pd_port_states[port_idx].c_state == CCG_STATUS_SINK) {
		pd_port_states[port_idx].current = type_c_current;
		pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;
	}

	if (pd_port_states[port_idx].c_state == CCG_STATUS_NOTHING) {
		pd_port_states[port_idx].current = 0;
		pd_port_states[port_idx].voltage = 0;
	}

	if (pd_port_states[port_idx].pd_state) {
		if (pd_port_states[port_idx].power_role == PD_ROLE_SINK) {
			pd_set_input_current_limit(port_idx, pd_current, pd_voltage);
			charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD, pd_current);
			pd_port_states[port_idx].current = pd_current;
			pd_port_states[port_idx].voltage = pd_voltage;
		} else {
			pd_set_input_current_limit(port_idx, 0, 0);
			/*Source*/
			pd_port_states[port_idx].current = rdo_max_current;
			pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;

		}
	} else {
		pd_set_input_current_limit(port_idx, 0, 0);
	}
#if DT_NODE_EXISTS(DT_ALIAS(gpio_mux_uart_flip))
	if (pd_port_states[CONFIG_PD_CCG6_EC_UART_DEBUG_PORT].c_state == CCG_STATUS_DEBUG ||
		pd_port_states[CONFIG_PD_CCG6_SOC_UART_DEBUG_PORT].c_state == CCG_STATUS_DEBUG) {
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_mux_uart_flip), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_mux_uart_flip), 0);
	}
#endif /* CONFIG_PD_CHIP_CCG6 */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
		charge_manager_update_dualrole(port_idx, CAP_DEDICATED);
	}

#ifdef CONFIG_PD_CCG8_EPR
	if (!!(epr_progress_status() & EPR_PROCESS_MASK) &&
	    !(epr_progress_status() & ~EPR_PROCESS_MASK)) {

		/* Handle EPR converstion through the buck switcher */
		if (pd_voltage > 20000) {
			/**
			 * (charge_ma * charge_mv / 20000 ) * 0.9 * 0.94
			 */
			calculate_ma =
				(int64_t)pd_current * (int64_t)pd_voltage * 90 * 95 / 200000000;
		} else {
			calculate_ma = (int64_t)pd_current * 88 / 100;
		}

		board_discharge_on_ac(0);
		charger_set_input_current_limit(0, (int)calculate_ma);
		clear_erp_progress_mask();
	}
#endif
}

void cypd_set_power_state(int power_state, int controller)
{
	int i;
	int rv = EC_SUCCESS;

	if (controller < 2) {
		rv = cypd_write_reg8_wait_ack(controller, CCG_SYS_PWR_STATE, power_state);
		if (rv != EC_SUCCESS) {
			CPRINTS("C%d, cypd set power_state 0x%02x failed, rv=%d",
				controller, power_state, rv);
		}
	} else {
		for (i = 0; i < PD_CHIP_COUNT; i++) {

			rv = cypd_write_reg8_wait_ack(i, CCG_SYS_PWR_STATE, power_state);
			if (rv != EC_SUCCESS) {
				CPRINTS("C%d, cypd set power_state 0x%02x failed, rv=%d",
					controller, power_state, rv);
			}
		}
	}
}

static int cypd_update_power_status(int controller)
{
	int i;
	int rv = EC_SUCCESS;
	int power_stat = 0;
	int pd_controller_is_sink = (prev_charge_port & 0x02) >> 1;
	bool battery_can_discharge = (battery_is_present() == BP_YES) &
		battery_get_disconnect_state();

	if (controller < PD_CHIP_COUNT) {
		if (battery_can_discharge)
			power_stat |= BIT(3);
		if ((extpower_is_present() && battery_can_discharge) ||
			(extpower_is_present() && controller != pd_controller_is_sink && prev_charge_port >=0))
			power_stat |= BIT(1) + BIT(2);

		CPRINTS("%s:%d=0x%x", __func__,controller, power_stat);
		rv = cypd_write_reg8_wait_ack(controller, CCG_POWER_STAT, power_stat);
	} else {
		for (i = 0; i < PD_CHIP_COUNT; i++) {
			power_stat = 0;
			if (battery_can_discharge)
				power_stat |= BIT(3);
			if ((extpower_is_present() && battery_can_discharge) ||
				(extpower_is_present() && i != pd_controller_is_sink && prev_charge_port >=0))
				power_stat |= BIT(1) + BIT(2);
			CPRINTS("%s:%d=0x%x", __func__,i, power_stat);
			rv = cypd_write_reg8_wait_ack(i, CCG_POWER_STAT, power_stat);
			if (rv != EC_SUCCESS)
				break;
		}
	}
	return rv;
}

static void port_to_safe_mode(int port)
{
	uint8_t data[2] = {0x00, CCG_PD_USER_MUX_CONFIG_SAFE};

	data[0] = PORT_TO_CONTROLLER_PORT(port);
	cypd_write_reg_block(PORT_TO_CONTROLLER(port), CCG_MUX_CFG_REG, data, 2);
	cypd_write_reg_block(PORT_TO_CONTROLLER(port), CCG_DEINIT_PORT_REG, data, 1);
	CPRINTS("P%d: Safe", port);

}

void cypd_set_power_active(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_S_CHANGE);
}

__overridable void cypd_customize_app_setup(int controller)
{
	/*
	 * CCG Chip behavior is different,
	 * use this function to customize setting
	 */
}

#ifdef CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE
static void pd_batt_init_deferred(void)
{
	cypd_customize_battery_cap();
	cypd_customize_battery_status();
}
DECLARE_DEFERRED(pd_batt_init_deferred);
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE */

static void cypd_handle_state(int controller)
{
	int data;
	int delay = 0;

	switch (pd_chip_config[controller].state) {
#ifdef CONFIG_PD_CHIP_CCG6
	case CCG_STATE_WAIT_STABLE:
		uint64_t timer = get_time().val;

		if (timer > CONFIG_PD_CCG6_WAIT_STABLE_TIMER * MSEC)
			pd_chip_config[controller].state = CCG_STATE_POWER_ON;

		if (controller == 0) {
			hook_call_deferred(&pd0_update_state_deferred_data,
				CONFIG_PD_CCG6_WAIT_STABLE_TIMER * MSEC);
		} else {
			hook_call_deferred(&pd1_update_state_deferred_data,
				CONFIG_PD_CCG6_WAIT_STABLE_TIMER * MSEC);
		}
		break;
#endif
	case CCG_STATE_POWER_ON:
		/* poll to see if the controller has booted yet */
		if (cypd_read_reg8(controller, CCG_DEVICE_MODE, &data) == EC_SUCCESS) {
			if ((data & 0x03) == 0x00) {
				CPRINTS("CYPD %d is in bootloader 0x%04x", controller, data);
				delay = 25*MSEC;
				if (cypd_read_reg16(controller, CCG_BOOT_MODE_REASON, &data)
						== EC_SUCCESS) {
					CPRINTS("CYPD bootloader reason 0x%02x", data);
				}

			} else
				pd_chip_config[controller].state = CCG_STATE_APP_SETUP;
		}
		/*try again in a while*/
		if (delay) {
			if (controller == 0)
				hook_call_deferred(&pd0_update_state_deferred_data, delay);
			else
				hook_call_deferred(&pd1_update_state_deferred_data, delay);
		} else
			task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0 << controller);
		break;

	case CCG_STATE_APP_SETUP:
			gpio_disable_interrupt(pd_chip_config[controller].gpio);
			cypd_get_version(controller);
			cypd_update_power_status(controller);

			update_system_power_state(controller);
			cypd_setup(controller);

			cypd_customize_app_setup(controller);

			/* After initial complete, update the type-c port state */
			cypd_update_port_state(controller, 0);
			cypd_update_port_state(controller, 1);

			ucsi_startup(controller);

			gpio_enable_interrupt(pd_chip_config[controller].gpio);

			/* Update PDO format after init complete */
			if (controller) {
#ifdef CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE
				hook_call_deferred(&pd_batt_init_deferred_data, 100 * MSEC);
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE */
				hook_call_deferred(&pdo_init_deferred_data, 25 * MSEC);
			}

			CPRINTS("CYPD %d Ready!", controller);
			pd_chip_config[controller].state = CCG_STATE_READY;
		break;
	default:
		CPRINTS("PD handle_state but in 0x%02x state!", pd_chip_config[controller].state);
		break;
	}

}


#ifdef CONFIG_PD_COMMON_VBUS_CONTROL
static uint8_t pd_c_fet_active_port;

DECLARE_DEFERRED(update_power_state_deferred);

int cypd_cfet_vbus_control(int port, bool enable, bool ec_control)
{
	int rv;
	int pd_controller = PORT_TO_CONTROLLER(port);
	int pd_port = PORT_TO_CONTROLLER_PORT(port);
	int regval = (ec_control ? CCG_EC_VBUS_CTRL_EN : 0) |
				(enable ? CCG_EC_VBUS_CTRL_ON : 0);

	if (port < 0 || port >= PD_PORT_COUNT) {
		return EC_ERROR_INVAL;
	}

	rv = cypd_write_reg8_wait_ack(pd_controller, CCG_PORT_VBUS_FET_CONTROL(pd_port),
		regval);
	if (rv != EC_SUCCESS)
		CPRINTS("%s:%d fail:%d", __func__, port, rv);

	if (enable)
		pd_c_fet_active_port |= BIT(port);
	else
		pd_c_fet_active_port &= ~BIT(port);

	return rv;
}

uint8_t cypd_get_cfet_status(void)
{
	return pd_c_fet_active_port;
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	int i;

	CPRINTS("%s port %d, prev:%d", __func__, charge_port, prev_charge_port);

	if (prev_charge_port == charge_port) {
		/* in the case of hard reset, we do not turn off the old
		 * port, but the PD will implicitly clear the port
		 * so we need to turn on the vbus control again.
		 */
		cypd_cfet_vbus_control(charge_port, true, true);
		return EC_SUCCESS;
	}


	if (prev_charge_port != -1 &&
		prev_charge_port != charge_port) {
		/* Turn off the previous charge port before turning on the next port */
		cypd_cfet_vbus_control(prev_charge_port, false, true);
	}

	for (i = 0; i < PD_PORT_COUNT; i++) {
		/* Just brute force all ports, we want to make sure
		 * we always update all ports in case a PD controller rebooted or some
		 * other error happens that we are not tracking state with.
		 */
		cypd_cfet_vbus_control(i, i == charge_port, true);
	}
	prev_charge_port = charge_port;
	hook_call_deferred(&update_power_state_deferred_data, 100 * MSEC);

	return EC_SUCCESS;
}
#endif /* CONFIG_PD_COMMON_VBUS_CONTROL */

static void print_pd_response_code(uint8_t controller, uint8_t port, uint8_t id, int len)
{
	if (verbose_msg_logging) {
		CPRINTS("PD Controller %d Port %d  Code 0x%02x %s Len: 0x%02x",
		controller,
		port,
		id,
		id & 0x80 ? "Response" : "Event",
		len);
	}
}




/*****************************************************************************/
/* Project */
int cypd_get_ac_power(void)
{
	int ac_power_mW = 0;

	/* -1 means no ac */
	if (prev_charge_port == -1)
		return 0;

	ac_power_mW = (pd_port_states[prev_charge_port].current
		* pd_port_states[prev_charge_port].voltage);

	return (ac_power_mW / 1000);
}

int cypd_get_active_port_voltage(void)
{
	return pd_port_states[prev_charge_port].voltage;
}

/*****************************************************************************/
/* Interrupt handler */

int cypd_device_int(int controller)
{
	int data;

	if (cypd_read_reg16(controller, CCG_RESPONSE_REG, &data) == EC_SUCCESS) {

		print_pd_response_code(controller, -1, data & 0xff, data>>8);

		switch (data & 0xFF) {
		case CCG_RESPONSE_RESET_COMPLETE:
			CPRINTS("PD%d Reset Complete", controller);
#ifdef CONFIG_PD_CHIP_CCG6
			if (pd_chip_config[controller].state != CCG_STATE_WAIT_STABLE)
#endif
				pd_chip_config[controller].state = CCG_STATE_POWER_ON;

			/* Run state handler to set up controller */
			task_set_event(TASK_ID_CYPD, 4 << controller);
			break;
		case CCG_RESPONSE_MESSAGE_QUEUE_OVERFLOW:
			CPRINTS("PD%d Message Overflow", controller);
			break;
		default:
			/* reduce the EC logs without debugging */
			if (verbose_msg_logging)
				CPRINTS("C%d device response: 0x%x", controller, data & 0xFF);
		}
	} else
		return EC_ERROR_INVAL;


	return EC_SUCCESS;
}
static bool pending_dp_poweroff[PD_PORT_COUNT];
static void poweroff_dp_check(void)
{
	int i;
	int alt_active = 0;

	for (i = 0; i < PD_PORT_COUNT; i++) {
		if (pending_dp_poweroff[i]) {
			/* see if alt mode is active */
			cypd_read_reg8(PORT_TO_CONTROLLER(i),
				CCG_DP_ALT_MODE_CONFIG_REG(PORT_TO_CONTROLLER_PORT(i)),
				&alt_active);
			/*
			 * DP_ALT should be on bit 1 always, but there is a bug
			 * in the PD stack that if a port does not have TBT mode
			 * enabled, it will shift the DP alt mode enable bit to
			 * bit 0. Since we only whitelist DP alt mode cards, just
			 * mask on both as a workaround.
			 */
			if ((alt_active & (BIT(1) + BIT(0))) == 0) {
				port_to_safe_mode(i);
			}
			pending_dp_poweroff[i] = 0;
		}
	}
}
static void poweroff_dp_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_DPALT_DISABLE);

}
DECLARE_DEFERRED(poweroff_dp_deferred);

struct framework_dp_ids {
	uint16_t vid;
	uint16_t pid;
} const cypd_altmode_ids[] = {
	{0x32AC, 0x0002},
	{0x32AC, 0x0003},
	{0x32AC, 0x000E},
};
struct match_vdm_header {
	uint8_t idx;
	uint8_t val;
} const framework_vdm_hdr_match[] = {
	{0, 0x8f},
	/*{1, 0x52},*/
	{2, 0},
	{4, 0x41},
	/*{5, 0xa0},*/
	{6, 0x00},
	{7, 0xFF},
	/*{8, 0xAC}, Framework VID */
	/*{9, 0x32}, */
	/*{10, 0x00},*/
	/*{11, 0x6C}*/
};

void cypd_handle_vdm(int controller, int port, uint8_t *data, int len)
{
	/* parse vdm
	 * if we get a DP alt mode VDM that matches our
	 * HDMI or DP VID/PID we will start a timer
	 * to set the port mux to safe/isolate
	 * if we get a enter alt mode later on,
	 * we will cancel the timer so that PD can
	 * properly enter the alt mode
	 *
	 *                       ID HDR            ProductVDO
	 *   hdr  SOP R VDMHDR   VDO      VDO      VDO
	 * HDMI
	 * 0x8f52 00 00 41a000ff ac32006c 00000000 00000200 18000000
	 * DP
	 * 0x8f52 00 00 41a000ff ac32006c 00000000 00000300 18000000
	 *   0 1  2  3  4        8        12       16
	 * 180W Power Adapter
	 * 0x8f59 00 00 41a800ff ac32c001 00000000 00000e00 01008020
	 */
	int i;
	uint16_t vid, pid;
	bool trigger_deferred_update = false;

	for (i = 0; i < sizeof(framework_vdm_hdr_match)/sizeof(struct match_vdm_header); i++) {
		if (framework_vdm_hdr_match[i].idx >= len) {
			continue;
		}
		if (data[framework_vdm_hdr_match[i].idx] !=
			framework_vdm_hdr_match[i].val) {
			return;
			}
	}

	for (i = 0; i < sizeof(cypd_altmode_ids)/sizeof(struct framework_dp_ids); i++) {
		vid = cypd_altmode_ids[i].vid;
		pid = cypd_altmode_ids[i].pid;
		if ((vid & 0xFF) == data[8] &&
			((vid>>8) & 0xFF) == data[9] &&
			(pid & 0xFF) == data[18] &&
			((pid>>8) & 0xFF) == data[19]
			) {
			pending_dp_poweroff[port + (controller<<1)] = true;
			trigger_deferred_update = true;
				CPRINTS(" vdm vidpid match");

		}

	}
	if (trigger_deferred_update) {
		hook_call_deferred(&poweroff_dp_deferred_data, 30000 * MSEC);
	}

}

void cypd_port_int(int controller, int port)
{
	int i, rv, response_len;
	uint8_t data2[32];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;
	int port_idx = (controller << 1) + port;
	enum tcpci_msg_type sop_type;
	static int snk_transition_flags;

	/* enum pd_msg_type sop_type; */
	rv = i2c_read_offset16_block(i2c_port, addr_flags,
		CCG_PORT_PD_RESPONSE_REG(port), data2, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("PORT_PD_RESPONSE_REG failed");

	print_pd_response_code(controller, port, data2[0], data2[1]);

	response_len = data2[1];
	switch (data2[0]) {
	case CCG_RESPONSE_PORT_DISCONNECT:
		record_ucsi_connector_change_event(controller, port);
		cypd_release_port(controller, port);
		CPRINTS("PORT_DISCONNECT");
		__fallthrough;
	case CCG_RESPONSE_HARD_RESET_RX:
	case CCG_RESPONSE_TYPE_C_ERROR_RECOVERY:
	case CCG_RESPONSE_HARD_RESET_SENT:
		if (data2[0] == CCG_RESPONSE_HARD_RESET_RX)
			CPRINTS("HARD_RESET_RX");
		if (data2[0] == CCG_RESPONSE_TYPE_C_ERROR_RECOVERY)
			CPRINTS("TYPE_C_ERROR_RECOVERY");
		if (data2[0] == CCG_RESPONSE_HARD_RESET_SENT)
			CPRINTS("CCG_RESPONSE_HARD_RESET_SENT");

#ifdef CONFIG_BOARD_LOTUS
		/* Assert prochot until the PMF is updated (Only sink role needs to do this) */
		if (pd_port_states[(controller << 1) + port].power_role == PD_ROLE_SINK &&
		   (prev_charge_port == (controller << 1) + port))
			update_pmf_events(BIT(PD_PROGRESS_DISCONNECTED), 1);

#ifdef CONFIG_PD_CCG8_EPR
		clear_erp_progress();
#endif
		set_gpu_gpio(GPIO_FUNC_ACDC, 0);
#endif

		cypd_update_port_state(controller, port);
		/* make sure the type-c state is cleared */
		clear_port_state(controller, port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_update_dualrole(port_idx, CAP_UNKNOWN);

		break;
	case CCG_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE:
		CPRINTS("CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE %d", port_idx);
		cypd_update_port_state(controller, port);
		cypd_set_prepare_pdo(controller, port);
#ifdef CONFIG_PD_CCG8_EPR
		/* make sure enter EPR mode only process in S0 state */
		if (chipset_in_state(CHIPSET_STATE_ON))
			cypd_enter_epr_mode(100);

#endif
		break;
	case CCG_RESPONSE_PORT_CONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_CONNECT %d", port_idx);
		record_ucsi_connector_change_event(controller, port);
		cypd_set_typec_profile(controller, port);
		break;
	case CCG_RESPONSE_SOURCE_CAP_MSG_RX:
		i2c_read_offset16_block(i2c_port, addr_flags,
				CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));

		if (data2[6] & BIT(7)) {
			pd_port_states[port_idx].epr_support = 1;
			CPRINTS("P%d EPR mode capable", port_idx);
		}

		snk_transition_flags = 1;
		break;
#ifdef CONFIG_PD_CCG8_EPR
	case CCG_RESPONSE_EPR_EVENT:
		CPRINTS("CCG_RESPONSE_EPR_EVENT %d", port_idx);
		cypd_update_epr_state(controller, port, response_len);
		cypd_update_port_state(controller, port);
		break;
#endif
	case CCG_RESPONSE_ACCEPT_MSG_RX:
		CPRINTS("CCG_RESPONSE_ACCEPT_MSG_RX %d", port_idx);
		if (snk_transition_flags) {
			charge_manager_force_ceil(port_idx, 500);
			snk_transition_flags = 0;
		}
		break;
	case CCG_RESPONSE_EXT_MSG_SOP_RX:
	case CCG_RESPONSE_EXT_SOP1_RX:
	case CCG_RESPONSE_EXT_SOP2_RX:
		if (data2[0] == CCG_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = TCPCI_MSG_SOP;
		else if (data2[0] == CCG_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = TCPCI_MSG_SOP_PRIME;
		else if (data2[0] == CCG_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = TCPCI_MSG_SOP_PRIME_PRIME;
#ifdef CONFIG_PD_COMMON_EXTENDED_MESSAGE
		cypd_handle_extend_msg(controller, port, response_len, sop_type);
		CPRINTS("CYP_RESPONSE_RX_EXT_MSG");
#endif /* CONFIG_PD_COMMON_EXTENDED_MESSAGE */
		break;
	case CCG_RESPONSE_OVER_CURRENT:
		CPRINTS("CCG_RESPONSE_OVER_CURRENT %d", port_idx);
		break;
	case CCG_RESPONSE_VDM_RX:
		i2c_read_offset16_block(i2c_port, addr_flags,
			CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));
		cypd_handle_vdm(controller, port, data2, response_len);
		CPRINTS("CCG_RESPONSE_VDM_RX");
		__fallthrough;
	default:
		if (response_len && verbose_msg_logging) {
			CPRINTF("Port:%d Data:0x", port_idx);
			i2c_read_offset16_block(i2c_port, addr_flags,
				CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));
			for (i = 0; i < response_len; i++)
				CPRINTF("%02x", data2[i]);
			CPRINTF("\n");
		}
		break;
	}
}

void cypd_interrupt(int controller)
{
	int data;
	int rv;
	int clear_mask = 0;

	rv = cypd_get_int(controller, &data);
	if (rv != EC_SUCCESS) {
		return;
	}

	if (data & CCG_DEV_INTR) {
		cypd_device_int(controller);
		clear_mask |= CCG_DEV_INTR;
	}

	if (data & CCG_PORT0_INTR) {
		cypd_port_int(controller, 0);
		clear_mask |= CCG_PORT0_INTR;
	}

	if (data & CCG_PORT1_INTR) {
		cypd_port_int(controller, 1);
		clear_mask |= CCG_PORT1_INTR;
	}

	if (data & CCG_ICLR_INTR)
		clear_mask |= CCG_ICLR_INTR;

	if (clear_mask)
		cypd_clear_int(controller, clear_mask);

	if (data & CCG_UCSI_INTR) {
		ucsi_read_tunnel(controller);
		cypd_clear_int(controller, CCG_UCSI_INTR);
	}
}

void pd0_chip_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(gpio_get_dt_spec(pd_chip_config[PD_CHIP_0].gpio)) == 0)
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_0);
}

void pd1_chip_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(gpio_get_dt_spec(pd_chip_config[PD_CHIP_1].gpio)) == 0)
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_1);
}

static void cypd_ucsi_wait_delay_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_UCSI_PPM_RESET);
}
DECLARE_DEFERRED(cypd_ucsi_wait_delay_deferred);

void cypd_usci_ppm_reset(void)
{
	/* wait PD chip finish UCSI process */
	hook_call_deferred(&cypd_ucsi_wait_delay_deferred_data, 500 * MSEC);
}

/*****************************************************************************/
/* CYPD task */

static int ucsi_tunnel_disabled;

void cypd_interrupt_handler_task(void *p)
{
	int i, j, evt;

	/* Initialize all charge suppliers to 0 */
	for (i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* trigger the handle_state to start setup in task */
	task_set_event(TASK_ID_CYPD, (CCG_EVT_STATE_CTRL_0 | CCG_EVT_STATE_CTRL_1));

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		cypd_enable_interrupt(i, true);
		task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0<<i);
	}


	while (1) {
		evt = task_wait_event(10*MSEC);

		if (firmware_update)
			continue;

		/*
		 * USCI PPM RESET will make PD current setting to default
		 * need setting port current again
		 */
		if (evt & CCG_EVT_UCSI_PPM_RESET)
			cypd_ppm_port_clear();

		if (evt & CCG_EVT_PDO_RESET)
			cypd_port_current_setting();

		if (evt & CCG_EVT_S_CHANGE)
			update_system_power_state(2);

		if (evt & CCG_EVT_INT_CTRL_0)
			cypd_interrupt(0);

		if (evt & CCG_EVT_INT_CTRL_1)
			cypd_interrupt(1);

		if (evt & CCG_EVT_STATE_CTRL_0) {
			cypd_handle_state(0);
			task_wait_event_mask(TASK_EVENT_TIMER,10);
		}

		if (evt & CCG_EVT_STATE_CTRL_1) {
			cypd_handle_state(1);
			task_wait_event_mask(TASK_EVENT_TIMER,10);
		}

		if (evt & CCG_EVT_PDO_INIT_0) {
			/* update new PDO format to select pdo register */
			cypd_pdo_init(0, 0, CCG_PD_CMD_SET_TYPEC_3A);
			cypd_pdo_init(1, 0, CCG_PD_CMD_SET_TYPEC_3A);
			task_wait_event_mask(TASK_EVENT_TIMER, 10);
			task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_INIT_1);
		}

		if (evt & CCG_EVT_PDO_INIT_1) {
			/* update new PDO format to select pdo register */
			cypd_pdo_init(0, 1, CCG_PD_CMD_SET_TYPEC_3A);
			cypd_pdo_init(1, 1, CCG_PD_CMD_SET_TYPEC_3A);
			task_wait_event_mask(TASK_EVENT_TIMER, 10);
		}

		if (evt & CCG_EVT_DPALT_DISABLE) {
			poweroff_dp_check();
		}

		if (evt & CCG_EVT_PDO_C0P0)
			cypd_set_typec_profile(0, 0);

		if (evt & CCG_EVT_PDO_C0P1)
			cypd_set_typec_profile(0, 1);

		if (evt & CCG_EVT_PDO_C1P0)
			cypd_set_typec_profile(1, 0);

		if (evt & CCG_EVT_PDO_C1P1)
			cypd_set_typec_profile(1, 1);

		if (evt & CCG_EVT_UPDATE_PWRSTAT)
			cypd_update_power_status(2);


		if (evt & (CCG_EVT_INT_CTRL_0 | CCG_EVT_INT_CTRL_1 |
					CCG_EVT_STATE_CTRL_0 | CCG_EVT_STATE_CTRL_1)) {
			/*
			 * If we just processed an event or sent some commands
			 * wait a bit for the pd controller to clear any pending
			 * interrupt requests
			 */
			usleep(50);
		}
		if (!ucsi_tunnel_disabled)
			check_ucsi_event_from_host();

		for (i = 0; i < PD_CHIP_COUNT; i++) {
			const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[i].gpio);
			if (gpio_pin_get_dt(intr) == 0) {
				task_set_event(TASK_ID_CYPD, 1<<i);
			}
		}
	}
}

/*****************************************************************************/
/* Commmon functions */

enum pd_power_role pd_get_power_role(int port)
{
	return pd_port_states[port].power_role;
}

void pd_request_power_swap(int port)
{
	/* We probably dont need to do this */
	return;
}

void pd_set_new_power_request(int port)
{
	/* We probably dont need to do this since we will always request max. */
	return;
}

int pd_is_connected(int port)
{
	return pd_port_states[port].c_state != CCG_STATUS_NOTHING;
}

int pd_get_active_current(int port)
{
	return pd_port_states[port].current;
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}



uint8_t *get_pd_version(int controller)
{
	return pd_chip_config[controller].version;
}

int active_charge_pd_chip(void)
{
	if (prev_charge_port == -1)
		return 0xff;

	return (prev_charge_port < 2) ? 0 : 1;
}

int get_active_charge_pd_port(void)
{
	/**
	 * charge_manager_get_active_charge_port() return the charger port too late,
	 * we need to get the active port status immediately.
	 */

	return prev_charge_port;
}

void update_active_charge_pd_port(int update_charger_port)
{
	CPRINTS("%s port %d, prev:%d", __func__, update_charger_port, prev_charge_port);

	prev_charge_port = update_charger_port;
}

void set_pd_fw_update(bool is_update)
{
	firmware_update = is_update;
}

bool get_pd_fw_update_status(void)
{
	return firmware_update;
}

void cypd_reinitialize(void)
{
	int i;

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		pd_chip_config[i].state = CCG_STATE_POWER_ON;
		/* Run state handler to set up controller */
		task_set_event(TASK_ID_CYPD, 4<<i);
	}
}

struct pd_port_current_state_t *get_pd_port_states_array(void)
{
	return pd_port_states;
}

int get_pd_alt_mode_status(int port)
{
	int alt_mode_status;

	cypd_read_reg8(PORT_TO_CONTROLLER(port),
		CCG_DP_ALT_MODE_CONFIG_REG(PORT_TO_CONTROLLER_PORT(port)),
		&alt_mode_status);

	return alt_mode_status;
}

/*****************************************************************************/
/* Host command */

/*****************************************************************************/
/* EC console command */

static int cmd_cypd_get_status(int argc, const char **argv)
{
	int i, p, data;
	uint8_t data16[16];
	char *e;

	static const char * const mode[] = {"Boot", "FW1", "FW2", "Invald"};
	static const char * const current_level[] = {"DefaultA", "1.5A", "3A", "InvA"};
	static const char * const port_status[] = {
		"Nothing", "Sink", "Source", "Debug", "Audio", "Powered Acc",
		"Unsupported", "Invalid"
	};
	static const char * const state[] = {
		"ERR", "POWER_ON", "APP_SETUP", "READY", "BOOTLOADER"
	};
	const struct gpio_dt_spec *intr;

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		intr = gpio_get_dt_spec(pd_chip_config[i].gpio);
		CPRINTS("PD%d INT value: %d", i, gpio_pin_get_dt(intr));
	}

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (i < PD_CHIP_COUNT) {
			CPRINTS("State: %s", state[pd_chip_config[i].state]);
			cypd_read_reg16(i, CCG_SILICON_ID, &data);
			CPRINTS("CYPD_SILICON_ID: 0x%04x", data);
			cypd_get_version(i);
			cypd_read_reg8(i, CCG_DEVICE_MODE, &data);
			CPRINTS("CYPD_DEVICE_MODE: 0x%02x %s", data, mode[data & 0x03]);
			cypd_read_reg_block(i, CCG_HPI_VERSION, data16, 4);
			CPRINTS("HPI_VERSION: 0x%02x%02x%02x%02x",
						data16[3], data16[2], data16[1], data16[0]);
			cypd_read_reg8(i, CCG_INTR_REG, &data);
			CPRINTS("CYPD_INTR_REG: 0x%02x %s %s %s %s",
						data,
						data & CCG_DEV_INTR ? "DEV" : "",
						data & CCG_PORT0_INTR ? "PORT0" : "",
						data & CCG_PORT1_INTR ? "PORT1" : "",
						data & CCG_UCSI_INTR ? "UCSI" : "");
			cypd_read_reg16(i, CCG_RESPONSE_REG, &data);
			CPRINTS("CYPD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CCG_PORT_PD_RESPONSE_REG(0), &data);
			CPRINTS("CYPD_PORT0_PD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CCG_PORT_PD_RESPONSE_REG(1), &data);
			CPRINTS("CYPD_PORT1_PD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg8(i, CCG_BOOT_MODE_REASON, &data);
			CPRINTS("CYPD_BOOT_MODE_REASON: 0x%02x", data);
			cypd_read_reg8(i, CCG_PDPORT_ENABLE_REG, &data);
			CPRINTS("CYPD_PDPORT_ENABLE_REG: 0x%04x", data);
			cypd_read_reg8(i, CCG_POWER_STAT, &data);
			CPRINTS("CYPD_POWER_STAT: 0x%02x", data);
			cypd_read_reg8(i, CCG_ICL_STS_REG, &data);
			CPRINTS("CCG_ICL_STS_REG: 0x%04x", data);
			cypd_read_reg8(i, CCG_SYS_PWR_STATE, &data);
			CPRINTS("CYPD_SYS_PWR_STATE: 0x%02x", data);
			for (p = 0; p < 2; p++) {
				CPRINTS("=====Port %d======", p);
				cypd_read_reg_block(i, CCG_PD_STATUS_REG(p), data16, 4);
				CPRINTS("PD_STATUS %s DataRole:%s PowerRole:%s Vconn:%s Partner:%s EPR:%s %sCable:%s",
						data16[1] & BIT(2) ? "Contract" : "NoContract",
						data16[0] & BIT(6) ? "DFP" : "UFP",
						data16[1] & BIT(0) ? "Source" : "Sink",
						data16[1] & BIT(5) ? "En" : "Dis",
						data16[2] & BIT(3) ? "Un-chunked" : "Chunked",
						data16[2] & BIT(7) ? "EPR" : "Non EPR",
						data16[1] & BIT(3) ? "EMCA " : "",
						data16[2] & BIT(6) ? "Active" : "Passive");
				cypd_read_reg8(i, CCG_TYPE_C_STATUS_REG(p), &data);
				CPRINTS("   TYPE_C_STATUS : %s %s %s %s %s",
							data & 0x1 ? "Connected" : "Not Connected",
							data & 0x2 ? "CC2" : "CC1",
							port_status[(data >> 2) & 0x7],
							data & 0x20 ? "Ra" : "NoRa",
							current_level[(data >> 6) & 0x03]);
				cypd_read_reg8(i, CCG_PORT_VBUS_FET_CONTROL(p), &data);
				CPRINTS("        VBUS_FET : %s %s",
						data & 0x1 ? "EC" : "Auto",
						data & 0x2 ? "On" : "Off");
				cypd_read_reg_block(i, CCG_CURRENT_RDO_REG(p), data16, 4);
				CPRINTS("             RDO : Current:%dmA MaxCurrent%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*10,
						*(uint32_t *)data16);

				cypd_read_reg_block(i, CCG_CURRENT_PDO_REG(p), data16, 4);
				CPRINTS("             PDO : MaxCurrent:%dmA Voltage%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*50,
						*(uint32_t *)data16);
				cypd_read_reg8(i, CCG_TYPE_C_VOLTAGE_REG(p), &data);
				CPRINTS("  TYPE_C_VOLTAGE : %dmV", data*100);
				cypd_read_reg8(i, CCG_PORT_CURRENT_REG(p), &data);
				CPRINTS("  TYPE_C_CURRENT : %dmA", data*50);
				cypd_read_reg_block(i, CCG_PORT_INTR_STATUS_REG(p), data16, 4);
				cypd_print_buff("      INTR_STATUS:", data16, 4);
				cypd_read_reg16(i, SELECT_SINK_PDO_EPR_MASK(p), &data);
				CPRINTS(" SINK PDO EPR MASK: 0x%02x", data);
				/* Flush console to avoid truncating output */
				cflush();
			}
			CPRINTS("=====UCSI======");
			cypd_read_reg16(i, CCG_VERSION_REG, &data);
			CPRINTS(" Version: 0x%02x", data);
			cypd_read_reg_block(i, CCG_CCI_REG, data16, 4);
			cypd_print_buff("     CCI:", data16, 4);
			cypd_read_reg_block(i, CCG_CONTROL_REG, data16, 8);
			cypd_print_buff(" Control:", data16, 8);
			cypd_read_reg_block(i, CCG_MESSAGE_IN_REG, data16, 16);
			cypd_print_buff(" Msg  In:", data16, 16);
			cypd_read_reg_block(i, CCG_MESSAGE_OUT_REG, data16, 16);
			cypd_print_buff(" Msg Out:", data16, 16);
		}

	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdstatus, cmd_cypd_get_status, "[number]",
			"Get Cypress PD controller status");

static int cmd_cypd_control(int argc, const char **argv)
{
	int i, enable;
	char *e;

	if (argc >= 3) {
		i = strtoi(argv[2], &e, 0);
		if (*e || i >= PD_CHIP_COUNT)
			return EC_ERROR_PARAM2;

		if (!strncmp(argv[1], "en", 2) || !strncmp(argv[1], "dis", 3)) {
			if (!parse_bool(argv[1], &enable))
				return EC_ERROR_PARAM1;
			if (enable)
				cypd_enable_interrupt(i, true);
			else
				cypd_enable_interrupt(i, false);
		} else if (!strncmp(argv[1], "reset", 5)) {
			cypd_write_reg8(i, CCG_PDPORT_ENABLE_REG, 0);
			/*can take up to 650ms to discharge port for disable*/
			cypd_wait_for_ack(i, 65);
			cypd_clear_int(i, CCG_DEV_INTR +
					  CCG_PORT0_INTR +
					  CCG_PORT1_INTR +
					  CCG_UCSI_INTR);
			usleep(50);
			CPRINTS("Full reset PD controller %d", i);
			/*
			 * see if we can talk to the PD chip yet - issue a reset command
			 * Note that we cannot issue a full reset command if the PD controller
			 * has a device attached - as it will return with an invalid command
			 * due to needing to disable all ports first.
			 */
			if (cypd_reset(i) == EC_SUCCESS) {
				CPRINTS("reset ok %d", i);
			}
		} else if (!strncmp(argv[1], "clearint", 8)) {
			cypd_clear_int(i, CCG_DEV_INTR +
					  CCG_PORT0_INTR +
					  CCG_PORT1_INTR +
					  CCG_UCSI_INTR);
		} else if (!strncmp(argv[1], "verbose", 7)) {
			verbose_msg_logging = (i != 0);
			CPRINTS("verbose=%d", verbose_msg_logging);
		} else if (!strncmp(argv[1], "ucsitun", 7)) {
			ucsi_tunnel_disabled = !i;
			CPRINTS("ucsi tun=%d", i);
		} else if (!strncmp(argv[1], "ucsi", 4)) {
			ucsi_set_debug(i != 0);
			CPRINTS("ucsi verbose=%d", i);
		} else if (!strncmp(argv[1], "powerstate", 10)) {
			int pwrstate;

			if (argc < 4)
				return EC_ERROR_PARAM3;
			pwrstate = strtoul(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
			cypd_set_power_state(pwrstate, 2);
		} else if (!strncmp(argv[1], "write16", 3)) {
			int r;
			int regval;
			if (argc < 5) {
				return EC_ERROR_PARAM4;
			}
			r = strtoul(argv[3], &e, 0);
			regval = strtoul(argv[4], &e, 0);
			cypd_write_reg16(i, r,  regval);
		} else if (!strncmp(argv[1], "write8", 3)) {
			int r;
			int regval;

			if (argc < 5)
				return EC_ERROR_PARAM4;
			r = strtoul(argv[3], &e, 0);
			regval = strtoul(argv[4], &e, 0);
			cypd_write_reg8(i, r,  regval);
		} else if (!strncmp(argv[1], "read16", 2)) {
			int r;
			int regval;

			if (argc < 4)
				return EC_ERROR_PARAM3;
			r = strtoul(argv[3], &e, 0);
			cypd_read_reg16(i, r,  &regval);
			CPRINTS("data=%d", regval);
		} else if (!strncmp(argv[1], "read8", 2)) {
			int r;
			int regval;

			if (argc < 4)
				return EC_ERROR_PARAM3;
			r = strtoul(argv[3], &e, 0);
			cypd_read_reg8(i, r,  &regval);
			CPRINTS("data=%d", regval);
		} else {
			return EC_ERROR_PARAM1;
		}
	} else {
		return EC_ERROR_PARAM_COUNT;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdctl, cmd_cypd_control,
			"[enable/disable/reset/clearint/verbose/ucsi] [controller]",
			"Set if handling is active for controller");


static int cmd_pdwrite(int argc, const char **argv)
{
	int controller, addr, data, rv;
	char *e;

	controller = strtoi(argv[1], &e, 0);
	addr = strtoi(argv[2], &e, 0);
	data = strtoi(argv[3], &e, 0);

	if (controller > 1)
		return EC_ERROR_PARAM1;

	CPRINTS("controller:%d ,addr:%x ,data:%d", controller, addr, data);

	rv = cypd_write_reg8_wait_ack(controller, addr, data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write data fail");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdwrite, cmd_pdwrite,
			"[controller] [addr] [data]",
			"write data to PD");

static int cmd_pdread(int argc, const char **argv)
{
	int controller, addr, data, rv;
	char *e;

	controller = strtoi(argv[1], &e, 0);
	addr = strtoi(argv[2], &e, 0);

	if (controller > 1)
		return EC_ERROR_PARAM1;

	rv = cypd_read_reg16(controller, addr, &data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write data fail");

	CPRINTS("controller:%d ,addr:%x ,data:%d", controller, addr, data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdread, cmd_pdread,
			"[controller] [addr]",
			"read data from PD");
