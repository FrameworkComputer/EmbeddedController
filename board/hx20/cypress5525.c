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

enum cyp5525_state {
    CYP5525_STATE_RESET,
    CYP5525_STATE_SETUP,
    CYP5525_STATE_READY,
    CYP5525_STATE_COUNT,
};

enum cyp5525_port_state {
    CYP5525_DEVICE_DETACH,
    CYP5525_DEVICE_ATTACH,
    CYP5525_DEVICE_ATTACH_WITH_CONTRACT,
    CYP5525_DEVICE_COUNT,
};

int state = CYP5525_STATE_RESET;

int offsetL2M(int offset){
    /* The LS byte of address is transferred first followed by the MS byte. */
    return ((offset & 0x00ff) << 8 | offset >> 8);
}

/* we need to do PD reset every power on */
int cyp5525_reset(void){
    /*
     * Device Reset: This command is used to request the CCG device to perform a soft reset
     * and start at the boot-loader stage again
     * Note: need barrel AC or battery
     */
    uint16_t data;
    data = 0x0152; /* Byte[0]:'R', Byte[1]:0x01 */
    return i2c_write_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_RESET_REG), data, 2);
}

int cyp5525_setup(void){
    /* 1. CCG notifies EC with "RESET Complete event after Reset/Power up/JUMP_TO_BOOT
     * 2. EC Reads DEVICE_MODE register does not in Boot Mode
     * 3. CCG will enters 100ms timeout window and waits for "EC Init Complete" command
     * 4. EC sets Source and Sink PDO mask if required
     * 5. EC sets Event mask if required
     * 6. EC sends EC Init Complete Command
     */

    int rv, data;

    /* First, read the INTR_REG to check the interrupt type */
    rv = i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_INTR_REG), &data, 1);
    CPRINTS("INTR_REG read value: 0x%02x", data);

    if((data & CYP5525_DEV_INTR) != CYP5525_DEV_INTR && rv == EC_SUCCESS){
        return EC_ERROR_INVAL;
    }

    /* Second, read the INTR_REG to check the CCG response event */
    rv = i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_RESPONSE_REG), &data, 2);
    CPRINTS("RESPONSE REG read value: 0x%02x", data);

    if(data != CYP5525_RESET_COMPLETE && rv == EC_SUCCESS){
        return EC_ERROR_INVAL;
    }

    /* Third, read the Device Mode Register */
    rv = i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_DEVICE_MODE), &data, 1);
    CPRINTS("DEVICE_MODE read value: 0x%02x", data);

    if((data & 0x03) == 0x00 && rv == EC_SUCCESS){
        /* TODO: If device mode in boot mode,do updated CCG FW flow*/
        return EC_ERROR_INVAL;
    }

    /* Clear the interrupt by writing 1 to the interrupt status bit to be cleared*/
    if (i2c_write_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_INTR_REG),
        CYP5525_DEV_INTR, 1))
        return EC_ERROR_INVAL;

    /* Set the port 0 event mask */
    if (i2c_write_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_EVENT_MASK_REG(0)),
        0xffff, 2))
        return EC_ERROR_INVAL;

    /* Set the port 1 event mask */
    if (i2c_write_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_EVENT_MASK_REG(1)),
        0xffff, 2))
        return EC_ERROR_INVAL;

    return EC_SUCCESS;
}

void cyp5525_intr(int port)
{
    int data;
    uint8_t data2[4];
    int active_current = 0;
    int active_voltage = 0;
    int state = CYP5525_DEVICE_DETACH;
    
    CPRINTS("C%d interrupt!", port);

    /*
    TODO: should we need to check the PD response register?
    i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_PD_RESPONSE_REG), &data, 2);
    CPRINTS("RESPONSE REG read value: 0x%02x", data);
    */
    i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_TYPE_C_STATUS_REG(port)), &data, 1);
    CPRINTS("DEVICE_MODE read value: 0x%02x", data);

    if( (data & CYP5525_PORT_CONNECTION) == CYP5525_PORT_CONNECTION)
    {
        state = CYP5525_DEVICE_ATTACH;
    }

    if( state == CYP5525_DEVICE_ATTACH)
    {
        /* Read the RDO if attach adaptor */

        i2c_read_offset16_block(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_PD_STATUS_REG(port)), data2, 4);

        if ((data2[1] & CYP5525_PD_CONTRACT_STATE) == CYP5525_PD_CONTRACT_STATE)
        {
            state = CYP5525_DEVICE_ATTACH_WITH_CONTRACT;
        }
        
        if ( state == CYP5525_DEVICE_ATTACH_WITH_CONTRACT )
        {
            i2c_read_offset16_block(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_CURRENT_PDO_REG(port)), data2, 4);
            active_current = (data2[0] + ((data2[1] & 0x3) << 8)) * 10;
            active_voltage = (((data2[1] & 0xFC) >> 2) + ((data2[2] & 0xF) << 6)) * 50;
            CPRINTS("C%d, current:%d mA, voltage:%d mV", port, active_current, active_voltage);
            /*i2c_read_offset16_block(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_CURRENT_RDO_REG(port)), &data2, 4);*/
        }
    }

    CPRINTS("C%d clear interrupt, state is %d", port, state);
    i2c_write_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_INTR_REG),
        (port ? CYP5525_PORT1_INTR : CYP5525_PORT0_INTR), 1);

}


void cyp5525_init(void)
{
	gpio_enable_interrupt(GPIO_EC_PD_INTA_L);

    if (cyp5525_reset()) 
    {
        CPRINTS("Cypress 5525 reset failed");
    }
    else 
    {
        state = CYP5525_STATE_SETUP;
        CPRINTS("Cypress 5525 reset done");
    }
	
}
DECLARE_HOOK(HOOK_INIT, cyp5525_init, HOOK_PRIO_DEFAULT);

static void pd_chip_deferred(void)
{
    int data;
    
    if (state == CYP5525_STATE_SETUP) 
    {
        if (cyp5525_setup())
        {
            CPRINTS("Cypress 5525 setup failed");
        }
        else
        {
            state = CYP5525_STATE_READY;
            CPRINTS("Cypress 5525 setup done");
        }
    }
    else if (state == CYP5525_STATE_READY)
    {
        i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_INTR_REG), &data, 1);
        CPRINTS("INTR_REG read value: 0x%02x", data);

        if ( (data & CYP5525_PORT0_INTR) == CYP5525_PORT0_INTR )
        {
            /* Process port 0 interrupt event */
            cyp5525_intr(0);
        }
        
        if ( (data & CYP5525_PORT1_INTR) == CYP5525_PORT1_INTR )
        {
            /* Process port 1 interrupt event */
            cyp5525_intr(1);
        }
    }
	
}
DECLARE_DEFERRED(pd_chip_deferred);

void pd_chip_interrupt(enum gpio_signal signal)
{
	/* debounce interrupt 10 msec */
	hook_call_deferred(&pd_chip_deferred_data, 10 * MSEC);
}
