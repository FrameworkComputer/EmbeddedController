/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* glados_pd board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Indicate which source is driving the ec_int line. */
static uint32_t ec_int_status;

static uint32_t pd_status_flags;

void pd_send_ec_int(void)
{
	/* If any sources are active, then drive the line low */
	gpio_set_level(GPIO_EC_INT, !ec_int_status);
}

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
	/*
	 * the DMA mapping is :
	 *  Chan 2 : TIM1_CH1  (C0 RX)
	 *  Chan 3 : SPI1_TX   (C0 TX)
	 *  Chan 4 : TIM3_CH1  (C1 RX)
	 *  Chan 5 : SPI2_TX   (C1 TX)
	 */
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Set PD MCU system status bits */
	if (system_jumped_to_this_image())
		pd_status_flags |= PD_STATUS_JUMPED_TO_IMAGE;
	if (system_get_image_copy() == SYSTEM_IMAGE_RW)
		pd_status_flags |= PD_STATUS_IN_RW;
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_C1_CC1_PD] = {"C1_CC1_PD", 3300, 4096, 0, STM32_AIN(0)},
	[ADC_C0_CC1_PD] = {"C0_CC1_PD", 3300, 4096, 0, STM32_AIN(2)},
	[ADC_C0_CC2_PD] = {"C0_CC2_PD", 3300, 4096, 0, STM32_AIN(4)},
	[ADC_C1_CC2_PD] = {"C1_CC2_PD", 3300, 4096, 0, STM32_AIN(5)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"slave", I2C_PORT_SLAVE, 1000, GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA}
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void tcpc_alert(int port)
{
	/*
	 * This function is called when the TCPC sets one of
	 * bits in the Alert register and that bit's corresponding
	 * location in the Alert_Mask register is set.
	 */
	atomic_or(&ec_int_status, port ?
		  PD_STATUS_TCPC_ALERT_1 : PD_STATUS_TCPC_ALERT_0);
	pd_send_ec_int();
}

void tcpc_alert_clear(int port)
{
	/*
	 * The TCPM has acknowledged all Alert bits and the
	 * Alert# line needs to be set inactive. Clear
	 * the corresponding port's bit in the static variable.
	 */
	atomic_clear(&ec_int_status, port ?
		  PD_STATUS_TCPC_ALERT_1 : PD_STATUS_TCPC_ALERT_0);
	pd_send_ec_int();
}

static void system_hibernate_deferred(void)
{
	ccprintf("EC requested hibernate\n");
	cflush();
	system_hibernate(0, 0);
}
DECLARE_DEFERRED(system_hibernate_deferred);

/****************************************************************************/
/* Console commands */
static int command_ec_int(int argc, char **argv)
{
	/* Indicate that ec_int gpio is active due to host command */
	atomic_or(&ec_int_status, PD_STATUS_HOST_EVENT);
	pd_send_ec_int();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ecint, command_ec_int,
			"",
			"Toggle EC interrupt line",
			NULL);

static int ec_status_host_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_status *p = args->params;
	struct ec_response_pd_status *r = args->response;

	/*
	 * ec_int_status is used to store state for HOST_EVENT,
	 * TCPC 0 Alert, and TCPC 1 Alert bits.
	 */
	r->status = ec_int_status | pd_status_flags;
	args->response_size = sizeof(*r);

	/* Have the PD follow the EC into hibernate. */
	if (p->status & EC_STATUS_HIBERNATING)
		hook_call_deferred(system_hibernate_deferred, 0);

	/*
	 * If the source of the EC int line was HOST_EVENT, it has
	 * been acknowledged so can always clear HOST_EVENT bit
	 * from the ec_int_status variable
	 */
	atomic_clear(&ec_int_status, PD_STATUS_HOST_EVENT);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_EXCHANGE_STATUS, ec_status_host_cmd,
		     EC_VER_MASK(EC_VER_PD_EXCHANGE_STATUS));

