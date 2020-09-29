/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip Crypress 5525 driver
 */

#include "config.h"
#include "console.h"
#include "cypress5525.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "uart.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

#define CONFIG_PD_CHIP_MAX_COUNT 2
#define USB_PD_CHIP_PORT_0	0
#define USB_PD_CHIP_PORT_1	1

static struct pd_chip_config_t pd_chip_config[CONFIG_PD_CHIP_MAX_COUNT] = {
    [USB_PD_CHIP_PORT_0] = {
        .i2c_port = I2C_PORT_PD_MCU,
        .addr_flags = CYP5525_ADDR0_FLAG,
        .state = CYP5525_STATE_POWER_ON,
    },
    [USB_PD_CHIP_PORT_1] = {
        .i2c_port = I2C_PORT_PD_MCU,
        .addr_flags = CYP5525_ADDR1_FLAG,
        .state = CYP5525_STATE_POWER_ON,
    },
};

int state = CYP5525_STATE_RESET;

int offsetL2M(int offset){
    /* The LS byte of address is transferred first followed by the MS byte. */
    return ((offset & 0x00ff) << 8 | offset >> 8);
}

/* we need to do PD reset every power on */
int cyp5525_reset(int port){
    /*
     * Device Reset: This command is used to request the CCG device to perform a soft reset
     * and start at the boot-loader stage again
     * Note: need barrel AC or battery
     */
    uint16_t data;
    uint16_t i2c_port = pd_chip_config[port].i2c_port;
    uint16_t addr_flags = pd_chip_config[port].addr_flags;

    data = 0x0152; /* Byte[0]:'R', Byte[1]:0x01 */
    return i2c_write_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_RESET_REG), data, 2);
}

int cyp5525_setup(int port){
    /* 1. CCG notifies EC with "RESET Complete event after Reset/Power up/JUMP_TO_BOOT
     * 2. EC Reads DEVICE_MODE register does not in Boot Mode
     * 3. CCG will enters 100ms timeout window and waits for "EC Init Complete" command
     * 4. EC sets Source and Sink PDO mask if required
     * 5. EC sets Event mask if required
     * 6. EC sends EC Init Complete Command
     */

    int rv, data;
    uint16_t i2c_port = pd_chip_config[port].i2c_port;
    uint16_t addr_flags = pd_chip_config[port].addr_flags;

    /* First, read the INTR_REG to check the interrupt type */
    rv = i2c_read_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_INTR_REG), &data, 1);
    CPRINTS("INTR_REG read value: 0x%02x", data);

    if((data & CYP5525_DEV_INTR) != CYP5525_DEV_INTR && rv == EC_SUCCESS){
        return EC_ERROR_INVAL;
    }

    /* Second, read the INTR_REG to check the CCG response event */
    rv = i2c_read_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_RESPONSE_REG), &data, 2);
    CPRINTS("RESPONSE REG read value: 0x%02x", data);

    if(data != CYP5525_RESET_COMPLETE && rv == EC_SUCCESS){
        return EC_ERROR_INVAL;
    }

    /* Third, read the Device Mode Register */
    rv = i2c_read_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_DEVICE_MODE), &data, 1);
    CPRINTS("DEVICE_MODE read value: 0x%02x", data);

    if((data & 0x03) == 0x00 && rv == EC_SUCCESS){
        /* TODO: If device mode in boot mode,do updated CCG FW flow*/
        return EC_ERROR_INVAL;
    }

    /* Clear the interrupt by writing 1 to the interrupt status bit to be cleared*/
    if (i2c_write_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_INTR_REG),
        CYP5525_DEV_INTR, 1))
        return EC_ERROR_INVAL;

    /* Set the port 0 event mask */
    if (i2c_write_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_EVENT_MASK_REG(0)),
        0xffff, 2))
        return EC_ERROR_INVAL;

    /* Set the port 1 event mask */
    if (i2c_write_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_EVENT_MASK_REG(1)),
        0xffff, 2))
        return EC_ERROR_INVAL;

    return EC_SUCCESS;
}

void cyp5525_intr(int pd_chip, int port)
{
    int data;
    uint8_t data2[4];
    int active_current = 0;
    int active_voltage = 0;
    int state = CYP5525_DEVICE_DETACH;
    uint16_t i2c_port = pd_chip_config[pd_chip].i2c_port;
    uint16_t addr_flags = pd_chip_config[pd_chip].addr_flags;
    
    CPRINTS("C%d interrupt!", port);

    /*
    TODO: should we need to check the PD response register?
    i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_PD_RESPONSE_REG), &data, 2);
    CPRINTS("RESPONSE REG read value: 0x%02x", data);
    */
    i2c_read_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_TYPE_C_STATUS_REG(port)), &data, 1);
    CPRINTS("DEVICE_MODE read value: 0x%02x", data);

    if( (data & CYP5525_PORT_CONNECTION) == CYP5525_PORT_CONNECTION)
    {
        state = CYP5525_DEVICE_ATTACH;
    }

    if( state == CYP5525_DEVICE_ATTACH)
    {
        /* Read the RDO if attach adaptor */

        i2c_read_offset16_block(i2c_port, addr_flags, offsetL2M(CYP5525_PD_STATUS_REG(port)), data2, 4);

        if ((data2[1] & CYP5525_PD_CONTRACT_STATE) == CYP5525_PD_CONTRACT_STATE)
        {
            state = CYP5525_DEVICE_ATTACH_WITH_CONTRACT;
        }
        
        if ( state == CYP5525_DEVICE_ATTACH_WITH_CONTRACT )
        {
            i2c_read_offset16_block(i2c_port, addr_flags, offsetL2M(CYP5525_CURRENT_PDO_REG(port)), data2, 4);
            active_current = (data2[0] + ((data2[1] & 0x3) << 8)) * 10;
            active_voltage = (((data2[1] & 0xFC) >> 2) + ((data2[2] & 0xF) << 6)) * 50;
            CPRINTS("C%d, current:%d mA, voltage:%d mV", port, active_current, active_voltage);
            /*i2c_read_offset16_block(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_CURRENT_RDO_REG(port)), &data2, 4);*/
            /* TODO: charge_manager to switch the VBUS */
        }
    }

    CPRINTS("C%d clear interrupt, state is %d", port, state);
    i2c_write_offset16(i2c_port, addr_flags, offsetL2M(CYP5525_INTR_REG),
        (port ? CYP5525_PORT1_INTR : CYP5525_PORT0_INTR), 1);

}

static void cype5525_second_chip_init(void)
{
    gpio_enable_interrupt(GPIO_EC_PD_INTB_L);

    if (!cyp5525_reset(USB_PD_CHIP_PORT_1))
    {
        pd_chip_config[USB_PD_CHIP_PORT_1].state = CYP5525_STATE_SETUP;
        CPRINTS("PD_chip_1 reset done");
    }
}
DECLARE_DEFERRED(cype5525_second_chip_init);

static void cyp5525_first_chip_init(void)
{
	gpio_enable_interrupt(GPIO_EC_PD_INTA_L);

    if (!cyp5525_reset(USB_PD_CHIP_PORT_0))
    {
        pd_chip_config[USB_PD_CHIP_PORT_0].state = CYP5525_STATE_SETUP;
        CPRINTS("PD_chip_0 reset done");
    }

    /* debounce 100 ms to wait PD chip 0 initail complete */
    hook_call_deferred(&cype5525_second_chip_init_data, (100 * MSEC));
}
DECLARE_HOOK(HOOK_INIT, cyp5525_first_chip_init, HOOK_PRIO_DEFAULT);

void cyp5525_interrupt(int port)
{
    int data;
    
    if (pd_chip_config[port].state == CYP5525_STATE_SETUP)
    {
        if (!cyp5525_setup(port))
        {
            pd_chip_config[port].state = CYP5525_STATE_READY;
            CPRINTS("PD_chip_%d setup done", port);
        }
    }
    else if (pd_chip_config[port].state == CYP5525_STATE_READY)
    {
        i2c_read_offset16(pd_chip_config[port].i2c_port, pd_chip_config[port].addr_flags,
            offsetL2M(CYP5525_INTR_REG), &data, 1);
        CPRINTS("INTR_REG read value: 0x%02x", data);

        /* Process PD chip port 0/1 interrupt event */
        cyp5525_intr(port, ((data & CYP5525_PORT0_INTR) == CYP5525_PORT0_INTR ? 0 : 1));

    }
	
}

void pd_chip_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_EC_PD_INTA_L:
		cyp5525_interrupt(USB_PD_CHIP_PORT_0);
		break;

	case GPIO_EC_PD_INTB_L:
		cyp5525_interrupt(USB_PD_CHIP_PORT_1);
		break;

	default:
		break;
	}
}
