/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip Crypress 5525 driver
 */

#ifndef __CROS_EC_CYPRESS5525_H
#define __CROS_EC_CYPRESS5525_H

/************************************************/
/*	REGISTER ADDRESS DEFINITION                 */
/************************************************/
#define CYP5525_DEVICE_MODE             0x0000
#define CYP5525_INTR_REG                0x0006
#define CYP5525_RESET_REG               0x0008
#define CYP5525_READ_ALL_VERSION_REG    0x0010
#define CYP5525_FW2_VERSION_REG         0x0020
#define CYP5525_PDPORT_ENABLE_REG       0x002C
#define CYP5525_UCSI_STATUS_REG         0x0038
#define CYP5525_UCSI_CONTROL_REG        0x0039
#define CYP5525_SYS_PWR_STATE           0x003B
#define CYP5525_RESPONSE_REG            0x007E
#define CYP5525_PD_RESPONSE_REG         0x1400
#define CYP5525_DATA_MEM_REG            0x1404
#define CYP5525_VERSION_REG             0xF000
#define CYP5525_CCI_REG                 0xF004
#define CYP5525_CONTROL_REG             0xF008
#define CYP5525_MESSAGE_IN_REG          0xF010
#define CYP5525_MESSAGE_OUT_REG         0xF020

#define CYP5525_SELECT_SINK_PDO_REG(x) \
	(0x1005 + (x * 0x1000))
#define CYP5525_PD_CONTROL_REG(x) \
	(0x1006 + (x * 0x1000))
#define CYP5525_PD_STATUS_REG(x) \
	(0x1008 + (x * 0x1000))
#define CYP5525_TYPE_C_STATUS_REG(x) \
	(0x100C + (x * 0x1000))
#define CYP5525_CURRENT_PDO_REG(x) \
	(0x1010 + (x * 0x1000))
#define CYP5525_CURRENT_RDO_REG(x) \
	(0x1014 + (x * 0x1000))
#define CYP5525_EVENT_MASK_REG(x) \
	(0x1024 + (x * 0x1000))
#define CYP5525_DP_ALT_MODE_CONFIG_REG(x) \
	(0x102B + (x * 0x1000))
#define CYP5525_PORT_INTR_STATUS_REG(x) \
	(0x1034 + (x * 0x1000))

#define CYP5525_SELECT_SINK_PDO_P1_REG      0x2005
#define CYP5525_PD_CONTROL_P1_REG           0x2006
#define CYP5525_PD_STATUS_P1_REG            0x2008
#define CYP5525_TYPE_C_STATUS_P1_REG        0x200C
#define CYP5525_CURRENT_PDO_P1_REG          0x2010
#define CYP5525_CURRENT_RDO_P1_REG          0x2014
#define CYP5525_EVENT_MASK_P1_REG           0x2024
#define CYP5525_DP_ALT_MODE_CONFIG_P1_REG   0x202B
#define CYP5525_PORT_INTR_STATUS_P1_REG     0x2034

/************************************************/
/*	DEVICE MODE DEFINITION                      */
/************************************************/
#define CYP5525_BOOT_MODE   0x00
#define CYP5525_FW1_MODE    0x01
#define CYP5525_FW2_MODE    0x02

/************************************************/
/*	DEVICE INTERRUPT DEFINITION                 */
/************************************************/
#define CYP5525_DEV_INTR    0x01
#define CYP5525_PORT0_INTR  0x02
#define CYP5525_PORT1_INTR  0x04
#define CYP5525_UCSI_INTR   0x80

/************************************************/
/*	PORT INTERRUPT DEFINITION                   */
/************************************************/
#define CYP5525_STATUS_TYPEC_ATTACH     0x00000001 /*bit 0 */
#define CYP5525_STATUS_TYPEC_DETACH     0x00000002 /*bit 1 */
#define CYP5525_STATUS_CONTRACT_DONE    0x00000004 /*bit 2 */
#define CYP5525_STATUS_PRSWAP_DONE      0x00000008 /*bit 3 */
#define CYP5525_STATUS_DRSWAP_DONE      0x00000010 /*bit 4 */
#define CYP5525_STATUS_VCONNSWAP_DONE   0x00000020 /*bit 5 */
#define CYP5525_STATUS_RESPONSE_READY   0x00200000 /*bit 21*/
#define CYP5525_STATUS_OVP_EVT          0x40000000 /*bit 30*/

/************************************************/
/*	PD PORT DEFINITION                          */
/************************************************/
#define CYP5525_PDPORT_DISABLE  0x00
#define CYP5525_PDPORT_ENABLE   0x01

/************************************************/
/*	RESPONSE DEFINITION                         */
/************************************************/
#define CYP5525_RESET_COMMAND_SUCCESS   0x02
#define CYP5525_RESET_COMMAND_FAILED    0x15
#define CYP5525_RESET_COMPLETE          0x80

/************************************************/
/*	TYPE-C STATUS DEFINITION                    */
/************************************************/
#define CYP5525_PORT_CONNECTION         0x01 /* bit 0 */
#define CYP5525_CC_POLARITY             0x02 /* bit 1 */
#define CYP5525_DEVICE_TYPE             0x1C /* bit 2-4 */
#define CYP5525_CURRENT_LEVEL           0xC0 /* bit 6-7 */

/************************************************/
/*	PD STATUS DEFINITION                        */
/************************************************/
#define CYP5525_PD_CONTRACT_STATE       0x04 /* bit 10 */


#define CYP5525_PD_SET_3A_PROF          0x02

/* chip/mchp/i2c.c will shift one bit to the left  */
#define CYP5525_ADDRESS0              0x10
#define CYP5525_ADDRESS1              0x80
#define CYP5525_ADDR0_FLAG            (CYP5525_ADDRESS0 >> 1)
#define CYP5525_ADDR1_FLAG            (CYP5525_ADDRESS1 >> 1)

enum cyp5525_state {
    CYP5525_STATE_POWER_ON,
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

struct pd_chip_config_t {
	uint16_t i2c_port;
	uint16_t addr_flags;
	enum cyp5525_state state;
};

/* PD CHIP */
void pd_chip_interrupt(enum gpio_signal signal);

#endif	/* __CROS_EC_CYPRESS5525_H */