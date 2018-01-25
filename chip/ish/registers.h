/* Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers and interrupts for Intel(R) Integrated Sensor Hub
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#ifndef __ASSEMBLER__
#include "common.h"

/* ISH GPIO has only one port */
#define DUMMY_GPIO_BANK 0

/*
 * ISH3.0 has 3 controllers. Locking must occur by-controller (not by-port).
 */
enum ish_i2c_port {
	ISH_I2C0   = 0,      /* Controller 0 */
	ISH_I2C1   = 1,      /* Controller 1 */
	ISH_I2C2   = 2,      /* Controller 2 */
	I2C_PORT_COUNT,
};

#endif

#define ISH_I2C_PORT_COUNT	I2C_PORT_COUNT

/* In ISH, the devices are mapped to pre-defined addresses in the 32-bit
 * linear address space.
 */
#define ISH_I2C0_BASE     0x00100000
#define ISH_I2C1_BASE     0x00102000
#define ISH_I2C2_BASE     0x00105000
#define ISH_UART_BASE     0x00103000
#define ISH_IPC_BASE      0x00B00000
#define ISH_IOAPIC_BASE   0xFEC00000
#define ISH_HPET_BASE     0xFED00000
#define ISH_LAPIC_BASE    0xFEE00000

/* HW interrupt pins mapped to IOAPIC, from I/O sources */
#define ISH_I2C0_IRQ               0
#define ISH_I2C1_IRQ               1
#define ISH_I2C2_IRQ               40
#define ISH_GPIO_IRQ               7
#define ISH_HPET_TIMER0_IRQ        55
#define ISH_HPET_TIMER1_IRQ        8
#define ISH_HPET_TIMER2_IRQ        11
#define ISH_IPC_HOST2ISH_IRQ       12
#define ISH_IPC_ISH2HOST_CLR_IRQ   24
#define ISH_UART0_IRQ              34
#define ISH_UART1_IRQ              35

/* Interrupt vectors 0-31 are architecture reserved.
 * Vectors 32-255 are user-defined.
 */
#define USER_VEC_START   32
/* Map IRQs to vectors after offset 10 for certain APIC interrupts */
#define IRQ_TO_VEC(irq)  (irq + USER_VEC_START + 10)

/* APIC interrupt vectors */
#define ISH_TS_VECTOR              0x20  /* Task switch vector */
#define LAPIC_LVT_ERROR_VECTOR     0x21
#define LAPIC_SPURIOUS_INT_VECTOR  0xff

/* Interrupt to vector mapping. To be programmed into IOAPIC */
#define ISH_I2C0_VEC               IRQ_TO_VEC(ISH_I2C0_IRQ)
#define ISH_I2C1_VEC               IRQ_TO_VEC(ISH_I2C1_IRQ)
#define ISH_I2C2_VEC               IRQ_TO_VEC(ISH_I2C2_IRQ)
#define ISH_GPIO_VEC               IRQ_TO_VEC(ISH_GPIO_IRQ)
#define ISH_HPET_TIMER0_VEC        IRQ_TO_VEC(ISH_HPET_TIMER0_IRQ)
#define ISH_HPET_TIMER1_VEC        IRQ_TO_VEC(ISH_HPET_TIMER1_IRQ)
#define ISH_HPET_TIMER2_VEC        IRQ_TO_VEC(ISH_HPET_TIMER2_IRQ)
#define ISH_IPC_ISH2HOST_CLR_VEC   IRQ_TO_VEC(ISH_IPC_ISH2HOST_CLR_IRQ)
#define ISH_UART0_VEC              IRQ_TO_VEC(ISH_UART0_IRQ)
#define ISH_UART1_VEC              IRQ_TO_VEC(ISH_UART1_IRQ)
#define ISH_IPC_VEC                IRQ_TO_VEC(ISH_IPC_HOST2ISH_IRQ)

#ifdef CONFIG_ISH_UART_0
#define ISH_DEBUG_UART       		UART_PORT_0
#define ISH_DEBUG_UART_IRQ   		ISH_UART0_IRQ
#define ISH_DEBUG_UART_VEC   		ISH_UART0_VEC
#else
#define ISH_DEBUG_UART       		UART_PORT_1
#define ISH_DEBUG_UART_IRQ   		ISH_UART1_IRQ
#define ISH_DEBUG_UART_VEC   		ISH_UART1_VEC
#endif

/* IPC_Registers */
#define IPC_PISR                   (ISH_IPC_BASE + 0x0)
#define IPC_PIMR                   (ISH_IPC_BASE + 0x4)
#define IPC_ISH2HOST_MSG_REGS      (ISH_IPC_BASE + 0x60)
#define IPC_ISH_FWSTS              (ISH_IPC_BASE + 0x34)
#define IPC_HOST2ISH_DOORBELL      (ISH_IPC_BASE + 0x48)
#define IPC_HOST2ISH_MSG_REGS      (ISH_IPC_BASE + 0xE0)
#define IPC_ISH2HOST_DOORBELL      (ISH_IPC_BASE + 0x54)
#define IPC_BUSY_CLEAR             (ISH_IPC_BASE + 0x378)

/* IOAPIC registers */
#define IOAPIC_IDX        0xFEC00000
#define IOAPIC_WDW        0xFEC00010
#define IOAPIC_EOI_REG    0xFEC00040

#define IOAPIC_VERSION               0x1
#define IOAPIC_IOREDTBL              0x10
#define IOAPIC_REDTBL_DELMOD_FIXED   0x00000000
#define IOAPIC_REDTBL_DESTMOD_PHYS   0x00000000
#define IOAPIC_REDTBL_INTPOL_HIGH    0x00000000
#define IOAPIC_REDTBL_INTPOL_LOW     0x00002000
#define IOAPIC_REDTBL_TRIGGER_EDGE   0x00000000
#define IOAPIC_REDTBL_TRIGGER_LEVEL  0x00008000
#define IOAPIC_REDTBL_MASK           0x00010000

/* LAPIC registers */
#define LAPIC_EOI_REG   0xFEE000B0
#define LAPIC_ISR_REG   0xFEE00170
#define LAPIC_ICR_REG   (ISH_LAPIC_BASE + 0x300)

#endif /* __CROS_EC_REGISTERS_H */
