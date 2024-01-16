/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for Microchip MEC152x family controllers
 */

#ifndef __CROS_EC_REGISTERS_H
#error "This header file should not be included directly."
#endif

/*
 * IRQ Numbers
 * NOTE: GIRQ22 aggregated output and its sources are not connected to
 * the NVIC.
 */
#define MCHP_IRQ_GIRQ8 0
#define MCHP_IRQ_GIRQ9 1
#define MCHP_IRQ_GIRQ10 2
#define MCHP_IRQ_GIRQ11 3
#define MCHP_IRQ_GIRQ12 4
#define MCHP_IRQ_GIRQ13 5
#define MCHP_IRQ_GIRQ14 6
#define MCHP_IRQ_GIRQ15 7
#define MCHP_IRQ_GIRQ16 8
#define MCHP_IRQ_GIRQ17 9
#define MCHP_IRQ_GIRQ18 10
#define MCHP_IRQ_GIRQ19 11
#define MCHP_IRQ_GIRQ20 12
#define MCHP_IRQ_GIRQ21 13
#define MCHP_IRQ_GIRQ23 14
#define MCHP_IRQ_GIRQ24 15
#define MCHP_IRQ_GIRQ25 16
#define MCHP_IRQ_GIRQ26 17
/* GIRQ13 direct sources */
#define MCHP_IRQ_I2C_0 20
#define MCHP_IRQ_I2C_1 21
#define MCHP_IRQ_I2C_2 22
#define MCHP_IRQ_I2C_3 23
#define MCHP_IRQ_I2C_4 158
#define MCHP_IRQ_I2C_5 168
#define MCHP_IRQ_I2C_6 169
#define MCHP_IRQ_I2C_7 170
/* GIRQ14 direct sources */
#define MCHP_IRQ_DMA_0 24
#define MCHP_IRQ_DMA_1 25
#define MCHP_IRQ_DMA_2 26
#define MCHP_IRQ_DMA_3 27
#define MCHP_IRQ_DMA_4 28
#define MCHP_IRQ_DMA_5 29
#define MCHP_IRQ_DMA_6 30
#define MCHP_IRQ_DMA_7 31
#define MCHP_IRQ_DMA_8 32
#define MCHP_IRQ_DMA_9 33
#define MCHP_IRQ_DMA_10 34
#define MCHP_IRQ_DMA_11 35
/* GIRQ15 direct sources */
#define MCHP_IRQ_UART0 40
#define MCHP_IRQ_UART1 41
#define MCHP_IRQ_EMI0 42
#define MCHP_IRQ_EMI1 43
#define MCHP_IRQ_UART2 44
#define MCHP_IRQ_ACPIEC0_IBF 45
#define MCHP_IRQ_ACPIEC0_OBE 46
#define MCHP_IRQ_ACPIEC1_IBF 47
#define MCHP_IRQ_ACPIEC1_OBE 48
#define MCHP_IRQ_ACPIEC2_IBF 49
#define MCHP_IRQ_ACPIEC2_OBE 50
#define MCHP_IRQ_ACPIEC3_IBF 51
#define MCHP_IRQ_ACPIEC3_OBE 52
#define MCHP_IRQ_ACPIPM1_CTL 55
#define MCHP_IRQ_ACPIPM1_EN 56
#define MCHP_IRQ_ACPIPM1_STS 57
#define MCHP_IRQ_8042EM_OBE 58
#define MCHP_IRQ_8042EM_IBF 59
#define MCHP_IRQ_MAILBOX_DATA 60
#define MCHP_IRQ_PORT80DBG0 62
#define MCHP_IRQ_PORT80DBG1 63
#define MCHP_IRQ_LASIC 64
/* GIRQ16 direct sources */
#define MCHP_IRQ_PKE_ERR 65
#define MCHP_IRQ_PKE_END 66
#define MCHP_IRQ_NDRNG 67
#define MCHP_IRQ_AES 68
#define MCHP_IRQ_HASH 69
/* GIRQ17 direct sources */
#define MCHP_IRQ_PECI_HOST 70
#define MCHP_IRQ_TACH_0 71
#define MCHP_IRQ_TACH_1 72
#define MCHP_IRQ_TACH_2 73
#define MCHP_IRQ_TACH_3 159
#define MCHP_IRQ_HDMI_CEC 160
#define MCHP_IRQ_ADC_SNGL 78
#define MCHP_IRQ_ADC_RPT 79
#define MCHP_IRQ_LED0_WDT 83
#define MCHP_IRQ_LED1_WDT 84
#define MCHP_IRQ_LED2_WDT 85
#define MCHP_IRQ_PROCHOT 87
/* GIRQ18 direct sources */
#define MCHP_IRQ_SLAVE_SPI 90
#define MCHP_IRQ_QMSPI0 91
#define MCHP_IRQ_PS2_0 100
#define MCHP_IRQ_PS2_1 101
#define MCHP_IRQ_PSPI 155
#define MCHP_IRQ_SGPIO_0 161
#define MCHP_IRQ_SGPIO_1 162
#define MCHP_IRQ_SGPIO_2 163
#define MCHP_IRQ_SGPIO_3 164
#define MCHP_IRQ_CCT_TMR 146
#define MCHP_IRQ_CCT_CAP0 147
#define MCHP_IRQ_CCT_CAP1 148
#define MCHP_IRQ_CCT_CAP2 149
#define MCHP_IRQ_CCT_CAP3 150
#define MCHP_IRQ_CCT_CAP4 151
#define MCHP_IRQ_CCT_CAP5 152
#define MCHP_IRQ_CCT_CMP0 153
#define MCHP_IRQ_CCT_CMP1 154
/* GIRQ19 direct sources */
#define MCHP_IRQ_ESPI_PC 103
#define MCHP_IRQ_ESPI_BM1 104
#define MCHP_IRQ_ESPI_BM2 105
#define MCHP_IRQ_ESPI_LTR 106
#define MCHP_IRQ_ESPI_OOB_UP 107
#define MCHP_IRQ_ESPI_OOB_DN 108
#define MCHP_IRQ_ESPI_FC 109
#define MCHP_IRQ_ESPI_RESET 110
#define MCHP_IRQ_ESPI_VW_EN 156
/* GIRQ20 direct sources */
#define MCHP_IRQ_OTP 173
/* GIRQ21 direct sources */
#define MCHP_IRQ_WDG 171
#define MCHP_IRQ_WEEK_ALARM 114
#define MCHP_IRQ_SUBWEEK 115
#define MCHP_IRQ_WEEK_SEC 116
#define MCHP_IRQ_WEEK_SUBSEC 117
#define MCHP_IRQ_WEEK_SYSPWR 118
#define MCHP_IRQ_RTC 119
#define MCHP_IRQ_RTC_ALARM 120
#define MCHP_IRQ_VCI_OVRD_IN 121
#define MCHP_IRQ_VCI_IN0 122
#define MCHP_IRQ_VCI_IN1 123
#define MCHP_IRQ_VCI_IN2 124
#define MCHP_IRQ_VCI_IN3 125
#define MCHP_IRQ_PS20A_WAKE 129
#define MCHP_IRQ_PS20B_WAKE 130
#define MCHP_IRQ_PS21B_WAKE 132
#define MCHP_IRQ_KSC_INT 135
/* GIRQ23 direct sources */
#define MCHP_IRQ_TIMER16_0 136
#define MCHP_IRQ_TIMER16_1 137
#define MCHP_IRQ_TIMER32_0 140
#define MCHP_IRQ_TIMER32_1 141
#define MCHP_IRQ_RTOS_TIMER 111
#define MCHP_IRQ_HTIMER0 112
#define MCHP_IRQ_HTIMER1 113
/* Must match CONFIG_IRQ_COUNT in config_chip.h */
#define MCHP_IRQ_MAX 174

/* Block base addresses */
#define MCHP_WDG_BASE 0x40000400
#define MCHP_TMR16_0_BASE 0x40000c00
#define MCHP_TMR32_0_BASE 0x40000c80
#define MCHP_DMA_BASE 0x40002400
#define MCHP_PROCHOT_BASE 0x40003400
#define MCHP_I2C0_BASE 0x40004000
#define MCHP_I2C1_BASE 0x40004400
#define MCHP_I2C2_BASE 0x40004800
#define MCHP_I2C3_BASE 0x40004C00
#define MCHP_I2C4_BASE 0x40005000
#define MCHP_I2C5_BASE 0x40005100
#define MCHP_I2C6_BASE 0x40005200
#define MCHP_I2C7_BASE 0x40005300
#define MCHP_QMSPI0_BASE 0x40070000
#define MCHP_PWM_0_BASE 0x40005800
#define MCHP_TACH_0_BASE 0x40006000
#define MCHP_PECI_BASE 0x40006400
#define MCHP_RTMR_BASE 0x40007400
#define MCHP_ADC_BASE 0x40007c00
#define MCHP_TFDP_BASE 0x40008c00
#define MCHP_HTIMER_BASE 0x40009800
#define MCHP_KEYSCAN_BASE 0x40009c00
#define MCHP_VBAT_BASE 0x4000a400
#define MCHP_VBAT_RAM_BASE 0x4000a800
#define MCHP_WKTIMER_BASE 0x4000ac80
#define MCHP_BBLED_0_BASE 0x4000B800
#define MCHP_INT_BASE 0x4000e000
#define MCHP_EC_BASE 0x4000fc00

#define MCHP_PCR_BASE 0x40080100
#define MCHP_GPIO_BASE 0x40081000

#define MCHP_MBOX_BASE 0x400f0000
#define MCHP_8042_BASE 0x400f0400
#define MCHP_ACPI_EC_0_BASE 0x400f0800
#define MCHP_ACPI_PM1_BASE 0x400f1c00
#define MCHP_UART0_BASE 0x400f2400
#define MCHP_UART1_BASE 0x400f2800
#define MCHP_UART2_BASE 0x400f2c00
#define MCHP_ESPI_IO_BASE 0x400f3400
#define MCHP_ESPI_MEM_BASE 0x400f3800
#define MCHP_EMI_0_BASE 0x400f4000
#define MCHP_EMI_1_BASE 0x400f4400
#define MCHP_P80CAP0_BASE 0x400f8000
#define MCHP_P80CAP1_BASE 0x400f8400
#define MCHP_ESPI_VW_BASE 0x400f9c00
#define MCHP_CHIP_BASE 0x400fff00

#ifndef __ASSEMBLER__

/*
 * Helper function for RAM address aliasing
 * NOTE: MCHP AHB masters do NOT require aliasing.
 * Cortex-M4 bit-banding does require aliasing of the
 * DATA SRAM region.
 */
#define MCHP_RAM_ALIAS(x) ((x) >= 0x118000 ? (x)-0x118000 + 0x20000000 : (x))

/* EC Chip Configuration */
/* 16-bit Device ID */
#define MCHP_CHIP_DEV_ID REG16(MCHP_CHIP_BASE + 0x1E)
/* 8-bit Device Sub ID */
#define MCHP_CHIP_DEV_SUB_ID REG8(MCHP_CHIP_BASE + 0x1D)
/* 8-bit Device Revision */
#define MCHP_CHIP_DEV_REV REG8(MCHP_CHIP_BASE + 0x1C)
/* All in one */
#define MCHP_CHIP_DEVRID32 REG32(MCHP_CHIP_BASE + 0x1C)
#define MCHP_CHIP_DEVID_POS 16
#define MCHP_CHIP_DEVID_MASK (0xfffful << MCHP_CHIP_DEVID_POS)
#define MCHP_CHIP_SUBID_POS 8
#define MCHP_CHIP_SUBID_MASK (0xfful << MCHP_CHIP_SUBID_POS)
#define MCHP_CHIP_REV_POS 0
#define MCHP_CHIP_REV_MASK (0xfful << MCHP_CHIP_REV_POS)
#define MCHP_CHIP_EXTRACT_DEVID(d) \
	(((uint32_t)(d) & MCHP_CHIP_DEVID_MASK) >> MCHP_CHIP_DEVID_POS)
#define MCHP_CHIP_EXTRACT_SUBID(d) \
	(((uint32_t)(d) & MCHP_CHIP_SUBID_MASK) >> MCHP_CHIP_SUBID_POS)
#define MCHP_CHIP_EXTRACT_REV(d) \
	(((uint32_t)(d) & MCHP_CHIP_REV_MASK) >> MCHP_CHIP_REV_POS)

/* PCR clock control dividers */
#define MCHP_PCR_CLK_CTL_FASTEST 1U
#define MCHP_PCR_CLK_CTL_48MHZ 1U
#define MCHP_PCR_CLK_CTL_12MHZ 4U

/*
 * PCR Peripheral Reset Lock register
 * MEC152x PCR Peripheral reset registers do not reset on
 * peripheral sleep. The peripheral is reset immediately.
 * Firmware must write an unlock value to this new lock
 * register, write to PCR reset enable register(s), and
 * write a lock value.
 */
#define MCHP_PCR_RST_LOCK REG32(MCHP_PCR_BASE + 0x84)
#define MCHP_PCR_RST_LOCK_VAL 0xa6382d4d
#define MCHP_PCR_RST_UNLOCK_VAL 0xa6382d4c

/* Number of PCR Sleep Enable, Clock Required, and Reset registers */
#define MCHP_PCR_SLP_RST_REG_MAX 5

/* MC152x new bit allow sleep entry when PLL is not locked */
#define MCHP_PCR_SYS_SLP_NO_PLL BIT(8)

/* Sleep 0: Sleep Enable, Clock Required, and Reset bits */
#define MCHP_PCR_JTAG BIT(0) /* CLKREQ only */
#define MCHP_PCR_OTP BIT(1)

/* Command all blocks to sleep */
#define MCHP_PCR_SLP_EN0_JTAG BIT(0)
#define MCHP_PCR_SLP_EN0_OTP BIT(1)
#define MCHP_PCR_SLP_EN0_SLEEP 0xffffffff

/*
 * Encode register number and bit position
 * b[4:0] = bit number
 * b[10:8] = zero based register number
 */
#define MCHP_PCR_ERB(rnum, bnum) ((((rnum) & 0x0f) << 8) | ((bnum) & 0x1f))

/* PCR Sleep 1: Sleep Enable, Clock Required, and Reset bits */
#define MCHP_PCR_BTMR16_1 MCHP_PCR_ERB(1, 31)
#define MCHP_PCR_BTMR16_0 MCHP_PCR_ERB(1, 30)
#define MCHP_PCR_ECS MCHP_PCR_ERB(1, 29)
#define MCHP_PCR_PWM8 MCHP_PCR_ERB(1, 27)
#define MCHP_PCR_PWM7 MCHP_PCR_ERB(1, 26)
#define MCHP_PCR_PWM6 MCHP_PCR_ERB(1, 25)
#define MCHP_PCR_PWM5 MCHP_PCR_ERB(1, 24)
#define MCHP_PCR_PWM4 MCHP_PCR_ERB(1, 23)
#define MCHP_PCR_PWM3 MCHP_PCR_ERB(1, 22)
#define MCHP_PCR_PWM2 MCHP_PCR_ERB(1, 21)
#define MCHP_PCR_PWM1 MCHP_PCR_ERB(1, 20)
#define MCHP_PCR_TACH3 MCHP_PCR_ERB(1, 13)
#define MCHP_PCR_TACH2 MCHP_PCR_ERB(1, 12)
#define MCHP_PCR_TACH1 MCHP_PCR_ERB(1, 11)
#define MCHP_PCR_I2C0 MCHP_PCR_ERB(1, 10)
#define MCHP_PCR_WDT MCHP_PCR_ERB(1, 9)
#define MCHP_PCR_CPU MCHP_PCR_ERB(1, 8)
#define MCHP_PCR_TFDP MCHP_PCR_ERB(1, 7)
#define MCHP_PCR_DMA MCHP_PCR_ERB(1, 6)
#define MCHP_PCR_PMC MCHP_PCR_ERB(1, 5)
#define MCHP_PCR_PWM0 MCHP_PCR_ERB(1, 4)
#define MCHP_PCR_TACH0 MCHP_PCR_ERB(1, 2)
#define MCHP_PCR_PECI MCHP_PCR_ERB(1, 1)
#define MCHP_PCR_ECIA MCHP_PCR_ERB(1, 0)

/* Command all blocks to sleep */
#define MCHP_PCR_SLP_EN1_BTMR16_1 BIT(31)
#define MCHP_PCR_SLP_EN1_BTMR16_0 BIT(30)
#define MCHP_PCR_SLP_EN1_ECS BIT(29)
#define MCHP_PCR_SLP_EN1_PWM_ALL (BIT(4) + (0xff << 20))
#define MCHP_PCR_SLP_EN1_PWM8 BIT(27)
#define MCHP_PCR_SLP_EN1_PWM7 BIT(26)
#define MCHP_PCR_SLP_EN1_PWM6 BIT(25)
#define MCHP_PCR_SLP_EN1_PWM5 BIT(24)
#define MCHP_PCR_SLP_EN1_PWM4 BIT(23)
#define MCHP_PCR_SLP_EN1_PWM3 BIT(22)
#define MCHP_PCR_SLP_EN1_PWM2 BIT(21)
#define MCHP_PCR_SLP_EN1_PWM1 BIT(20)
#define MCHP_PCR_SLP_EN1_TACH3 BIT(13)
#define MCHP_PCR_SLP_EN1_TACH2 BIT(12)
#define MCHP_PCR_SLP_EN1_TACH1 BIT(11)
#define MCHP_PCR_SLP_EN1_I2C0 BIT(10)
#define MCHP_PCR_SLP_EN1_WDT BIT(9)
#define MCHP_PCR_SLP_EN1_CPU BIT(8)
#define MCHP_PCR_SLP_EN1_TFDP BIT(7)
#define MCHP_PCR_SLP_EN1_DMA BIT(6)
#define MCHP_PCR_SLP_EN1_PMC BIT(5)
#define MCHP_PCR_SLP_EN1_PWM0 BIT(4)
#define MCHP_PCR_SLP_EN1_TACH0 BIT(2)
#define MCHP_PCR_SLP_EN1_PECI BIT(1)
#define MCHP_PCR_SLP_EN1_ECIA BIT(0)
/* all sleep enable 1 bits */
#define MCHP_PCR_SLP_EN1_SLEEP 0xffffffff
/*
 * block not used by default
 * Do not sleep ECIA, PMC, CPU and ECS
 */
#define MCHP_PCR_SLP_EN1_UNUSED_BLOCKS 0xdffffede

/* PCR Sleep 2: Sleep Enable, Clock Required 2, Reset bits */
#define MCHP_PCR_GLUE MCHP_PCR_ERB(2, 29)
#define MCHP_PCR_UART2 MCHP_PCR_ERB(2, 28)
#define MCHP_PCR_SAF MCHP_PCR_ERB(2, 27)
#define MCHP_PCR_P80CAP1 MCHP_PCR_ERB(2, 26)
#define MCHP_PCR_P80CAP0 MCHP_PCR_ERB(2, 25)
#define MCHP_PCR_ASIF MCHP_PCR_ERB(2, 24)
#define MCHP_PCR_ACPI_EC3 MCHP_PCR_ERB(2, 22)
#define MCHP_PCR_ACPI_EC2 MCHP_PCR_ERB(2, 21)
#define MCHP_PCR_ESPI_SCR MCHP_PCR_ERB(2, 20)
#define MCHP_PCR_ESPI MCHP_PCR_ERB(2, 19)
#define MCHP_PCR_RTC MCHP_PCR_ERB(2, 18)
#define MCHP_PCR_MBOX MCHP_PCR_ERB(2, 17)
#define MCHP_PCR_8042 MCHP_PCR_ERB(2, 26)
#define MCHP_PCR_ACPI_PM1 MCHP_PCR_ERB(2, 15)
#define MCHP_PCR_ACPI_EC1 MCHP_PCR_ERB(2, 14)
#define MCHP_PCR_ACPI_EC0 MCHP_PCR_ERB(2, 13)
#define MCHP_PCR_GCFG MCHP_PCR_ERB(2, 12)
#define MCHP_PCR_UART1 MCHP_PCR_ERB(2, 2)
#define MCHP_PCR_UART0 MCHP_PCR_ERB(2, 1)
#define MCHP_PCR_EMI0 MCHP_PCR_ERB(2, 0)

/* Command all blocks to sleep */
#define MCHP_PCR_SLP_EN2_GLUE BIT(29)
#define MCHP_PCR_SLP_EN2_UART2 BIT(28)
#define MCHP_PCR_SLP_EN2_SAF BIT(27)
#define MCHP_PCR_SLP_EN2_P80CAP1 BIT(26)
#define MCHP_PCR_SLP_EN2_P80CAP0 BIT(25)
#define MCHP_PCR_SLP_EN2_ASIF BIT(24)
#define MCHP_PCR_SLP_EN2_ACPI_EC3 BIT(22)
#define MCHP_PCR_SLP_EN2_ACPI_EC2 BIT(21)
#define MCHP_PCR_SLP_EN2_ESPI_SCR BIT(20)
#define MCHP_PCR_SLP_EN2_ESPI BIT(19)
#define MCHP_PCR_SLP_EN2_RTC BIT(18)
#define MCHP_PCR_SLP_EN2_MAILBOX BIT(17)
#define MCHP_PCR_SLP_EN2_MIF8042 BIT(16)
#define MCHP_PCR_SLP_EN2_ACPI_PM1 BIT(15)
#define MCHP_PCR_SLP_EN2_ACPI_EC1 BIT(14)
#define MCHP_PCR_SLP_EN2_ACPI_EC0 BIT(13)
#define MCHP_PCR_SLP_EN2_GCFG BIT(12)
#define MCHP_PCR_SLP_EN2_UART1 BIT(2)
#define MCHP_PCR_SLP_EN2_UART0 BIT(1)
#define MCHP_PCR_SLP_EN2_EMI0 BIT(0)
/* all sleep enable 2 bits */
#define MCHP_PCR_SLP_EN2_SLEEP 0xffffffff

/* PCR Sleep 3: Sleep Enable, Clock Required, and Reset */
#define MCHP_PCR_CCT0 MCHP_PCR_ERB(3, 30)
#define MCHP_PCR_HTMR1 MCHP_PCR_ERB(3, 29)
#define MCHP_PCR_AESHASH MCHP_PCR_ERB(3, 28)
#define MCHP_PCR_RNG MCHP_PCR_ERB(3, 27)
#define MCHP_PCR_PKE MCHP_PCR_ERB(3, 26)
#define MCHP_PCR_BTMR32_1 MCHP_PCR_ERB(3, 24)
#define MCHP_PCR_BTMR32_0 MCHP_PCR_ERB(3, 23)
#define MCHP_PCR_I2C4 MCHP_PCR_ERB(3, 20)
#define MCHP_PCR_LED2 MCHP_PCR_ERB(3, 18)
#define MCHP_PCR_LED1 MCHP_PCR_ERB(3, 17)
#define MCHP_PCR_LED0 MCHP_PCR_ERB(3, 16)
#define MCHP_PCR_I2C3 MCHP_PCR_ERB(3, 15)
#define MCHP_PCR_I2C2 MCHP_PCR_ERB(3, 14)
#define MCHP_PCR_I2C1 MCHP_PCR_ERB(3, 13)
#define MCHP_PCR_KEYSCAN MCHP_PCR_ERB(3, 11)
#define MCHP_PCR_HTMR0 MCHP_PCR_ERB(3, 10)
#define MCHP_PCR_PS2_1 MCHP_PCR_ERB(3, 6)
#define MCHP_PCR_PS2_0 MCHP_PCR_ERB(3, 5)
#define MCHP_PCR_ADC MCHP_PCR_ERB(3, 3)
#define MCHP_PCR_HDMI_CEC MCHP_PCR_ERB(3, 1)

/* Command blocks to sleep */
#define MCHP_PCR_SLP_EN3_CCT0 BIT(30)
#define MCHP_PCR_SLP_EN3_HTMR1 BIT(29)
#define MCHP_PCR_SLP_EN3_AESHASH BIT(28)
#define MCHP_PCR_SLP_EN3_RNG BIT(27)
#define MCHP_PCR_SLP_EN3_PKE BIT(26)
#define MCHP_PCR_SLP_EN3_BTMR32_1 BIT(24)
#define MCHP_PCR_SLP_EN3_BTMR32_0 BIT(23)
#define MCHP_PCR_SLP_EN3_I2C4 BIT(20)
#define MCHP_PCR_SLP_EN3_LED2 BIT(18)
#define MCHP_PCR_SLP_EN3_LED1 BIT(17)
#define MCHP_PCR_SLP_EN3_LED0 BIT(16)
#define MCHP_PCR_SLP_EN3_I2C3 BIT(15)
#define MCHP_PCR_SLP_EN3_I2C2 BIT(14)
#define MCHP_PCR_SLP_EN3_I2C1 BIT(13)
#define MCHP_PCR_SLP_EN3_KEYSCAN BIT(11)
#define MCHP_PCR_SLP_EN3_HTMR0 BIT(10)
#define MCHP_PCR_SLP_EN3_PS2_1 BIT(6)
#define MCHP_PCR_SLP_EN3_PS2_0 BIT(5)
#define MCHP_PCR_SLP_EN3_ADC BIT(3)
#define MCHP_PCR_SLP_EN3_HDMI_CEC BIT(1)
#define MCHP_PCR_SLP_EN3_ALL_CRYPTO (0x07 << 26)
/* all sleep enable 3 bits */
#define MCHP_PCR_SLP_EN3_SLEEP 0xfffffffd
#define MCHP_PCR_SLP_EN3_PWM_ALL 0

/* PCR Sleep 4: Sleep Enable, Clock Required, Reset */
#define MCHP_PCR_SGPIO3 MCHP_PCR_ERB(4, 20)
#define MCHP_PCR_SGPIO2 MCHP_PCR_ERB(4, 19)
#define MCHP_PCR_SGPIO1 MCHP_PCR_ERB(4, 18)
#define MCHP_PCR_SGPIO0 MCHP_PCR_ERB(4, 17)
#define MCHP_PCR_SLV_SPI MCHP_PCR_ERB(4, 16)
#define MCHP_PCR_PSPI MCHP_PCR_ERB(4, 14)
#define MCHP_PCR_PROCHOT MCHP_PCR_ERB(4, 13)
#define MCHP_PCR_I2C7 MCHP_PCR_ERB(4, 12)
#define MCHP_PCR_I2C6 MCHP_PCR_ERB(4, 11)
#define MCHP_PCR_I2C5 MCHP_PCR_ERB(4, 10)
#define MCHP_PCR_QMSPI MCHP_PCR_ERB(4, 8)
#define MCHP_PCR_RTMR MCHP_PCR_ERB(4, 6)

/* Command blocks to sleep */
#define MCHP_PCR_SLP_EN4_SGPIO3 BIT(20)
#define MCHP_PCR_SLP_EN4_SGPIO2 BIT(19)
#define MCHP_PCR_SLP_EN4_SGPIO1 BIT(18)
#define MCHP_PCR_SLP_EN4_SGPIO0 BIT(17)
#define MCHP_PCR_SLP_EN4_SLV_SPI BIT(16)
#define MCHP_PCR_SLP_EN4_PSPI BIT(14)
#define MCHP_PCR_SLP_EN4_PROCHOT BIT(13)
#define MCHP_PCR_SLP_EN4_I2C7 BIT(12)
#define MCHP_PCR_SLP_EN4_I2C6 BIT(11)
#define MCHP_PCR_SLP_EN4_I2C5 BIT(10)
#define MCHP_PCR_SLP_EN4_QMSPI BIT(8)
#define MCHP_PCR_SLP_EN4_RTMR BIT(6)
/* all sleep enable 4 bits */
#define MCHP_PCR_SLP_EN4_SLEEP 0xffffffff
#define MCHP_PCR_SLP_EN4_PWM_ALL 0

/* Allow all blocks to request clocks */
#define MCHP_PCR_SLP_EN0_WAKE (~(MCHP_PCR_SLP_EN0_SLEEP))
#define MCHP_PCR_SLP_EN1_WAKE (~(MCHP_PCR_SLP_EN1_SLEEP))
#define MCHP_PCR_SLP_EN2_WAKE (~(MCHP_PCR_SLP_EN2_SLEEP))
#define MCHP_PCR_SLP_EN3_WAKE (~(MCHP_PCR_SLP_EN3_SLEEP))
#define MCHP_PCR_SLP_EN4_WAKE (~(MCHP_PCR_SLP_EN4_SLEEP))

/* Bit defines for MCHP_PCR_PWR_RST_STS */
#define MCHP_PWR_RST_STS_MASK_RO 0xc8c
#define MCHP_PWR_RST_STS_MASK_RWC 0x170
#define MCHP_PWR_RST_STS_MASK \
	((MCHP_PWR_RST_STS_MASK_RO) | (MCHP_PWR_RST_STS_MASK_RWC))

#define MCHP_PWR_RST_STS_ESPI_CLK_ACT BIT(11) /* RO */
#define MCHP_PWR_RST_STS_32K_ACT BIT(10) /* RO */
#define MCHP_PWR_RST_STS_WDT BIT(8) /* R/WC */
#define MCHP_PWR_RST_STS_JTAG_RSTN BIT(7) /* RO */
#define MCHP_PWR_RST_STS_SYS BIT(6) /* R/WC */
#define MCHP_PWR_RST_STS_VBAT BIT(5) /* R/WC */
#define MCHP_PWR_RST_STS_VTR BIT(4) /* R/WC */
#define MCHP_PWR_RST_STS_HOST BIT(3) /* RO */
#define MCHP_PWR_RST_STS_VCC_PWRGD BIT(2) /* RO */

/* Bit defines for MCHP_PCR_PWR_RST_CTL */
#define MCHP_PCR_PWR_HOST_RST_SEL_BITPOS 8
#define MCHP_PCR_PWR_HOST_RST_PCI_RESET BIT(8)
#define MCHP_PCR_PWR_HOST_RST_ESPI_PLTRST (0 << 8)
#define MCHP_PCR_PWR_OK_INV_BITPOS 0

/* Bit defines for MCHP_PCR_SYS_RST */
#define MCHP_PCR_SYS_SOFT_RESET BIT(8)

/* EC Subsystem */
#define MCHP_EC_AHB_ERR REG32(MCHP_EC_BASE + 0x04)
#define MCHP_EC_ID_RO REG32(MCHP_EC_BASE + 0x10)
#define MCHP_EC_AHB_ERR_EN REG32(MCHP_EC_BASE + 0x14)
#define MCHP_EC_INT_CTRL REG32(MCHP_EC_BASE + 0x18)
#define MCHP_EC_TRACE_EN REG32(MCHP_EC_BASE + 0x1c)
#define MCHP_EC_JTAG_EN REG32(MCHP_EC_BASE + 0x20)
#define MCHP_EC_WDT_CNT REG32(MCHP_EC_BASE + 0x28)
#define MCHP_EC_AES_SHA_SWAP_CTRL REG8(MCHP_EC_BASE + 0x2c)
#define MCHP_EC_VCI_FW_OVRD REG8(MCHP_EC_BASE + 0x50)
#define MCHP_EC_CRYPTO_SRESET REG8(MCHP_EC_BASE + 0x5c)
#define MCHP_EC_GPIO_BANK_PWR REG8(MCHP_EC_BASE + 0x64)
#define MCHP_EC_SLP_STS_MIRROR REG8(MCHP_EC_BASE + 0x114)

/* AHB ERR Enable bit[0]=0(enable), 1(disable) */
#define MCHP_EC_AHB_ERROR_ENABLE 0
#define MCHP_EC_AHB_ERROR_DISABLE 1

/* MCHP_EC_JTAG_EN bit definitions */
#define MCHP_JTAG_ENABLE 0x01
/* bits [2:1] */
#define MCHP_JTAG_MODE_4PIN 0x00
/* ARM 2-pin SWD plus 1-pin Serial Wire Viewer (ITM) */
#define MCHP_JTAG_MODE_SWD_SWV 0x02
/* ARM 2-pin SWD with no SWV */
#define MCHP_JTAG_MODE_SWD 0x04

/* MCHP_EC_CRYPTO_SRESET bit definitions. Bits cleared by HW */
#define MCHP_CRYPTO_NDRNG_SRST 0x01
#define MCHP_CRYPTO_PKE_SRST 0x02
#define MCHP_CRYPTO_AES_SHA_SRST 0x04
#define MCHP_CRYPTO_ALL_SRST 0x07

/* MCHP_GPIO_BANK_PWR bit definitions */
#define MCHP_EC_GPIO_BANK_PWR_MASK 0x86
#define MCHP_EC_GPIO_BANK_PWR_VTR2_18 0x02
#define MCHP_EC_GPIO_BANK_PWR_VTR3_18 0x04
#define MCHP_EC_GPIO_BANK_PWR_LOCK 0x80

/* EC Interrupt aggregator (ECIA) */
#define MCHP_INT_GIRQ_LEN 20 /* 5 32-bit registers */
#define MCHP_INT_GIRQ_FIRST 8
#define MCHP_INT_GIRQ_LAST 26
#define MCHP_INT_GIRQ_NUM (26 - 8 + 1)
/* MCHP_INT_GIRQ_FIRST <= x <= MCHP_INT_GIRQ_LAST */
#define MCHP_INTx_BASE(x) (MCHP_INT_BASE + (((x)-8) * MCHP_INT_GIRQ_LEN))

/*
 * GPIO GIRQ's are not direct capable
 * GIRQ08 GPIO 0140 - 0176
 * GIRQ09 GPIO 0100 - 0136
 * GIRQ10 GPIO 040 - 076
 * GIRQ11 GPIO 000 - 036
 * GIRQ12 GPIO 0200 - 0236
 * GIRQ26 GPIO 0240 - 0276
 * Other GIRQ's not direct capable:
 * GIRQ22 wake peripheral clock only
 * GIRQ24, GIRQ25 eSPI host to endpoint virtual wires
 */
#define MCHP_INT_AGGR_ONLY_BITMAP 0x07401F00U
#define MCHP_INT_DIRECT_CAPABLE_BITMAP 0x00BFE000U

/* GIRQ13 I2C controllers. Direct capable */
#define MCHP_INT13_I2C(x) (1ul << (x))

/* GIRQ14 DMA channels 0 - 11. Direct capable */
#define MCHP_INT14_DMA(x) (1ul << (x))

/* GIQ15 interrupt sources. Direct capable */
#define MCHP_INT15_UART_0 BIT(0)
#define MCHP_INT15_UART_1 BIT(1)
#define MCHP_INT15_UART_2 BIT(4)
#define MCHP_INT15_EMI_0 BIT(2)
#define MCHP_INT15_EMI_1 BIT(3)
#define MCHP_INT15_ACPI_EC0_IBF BIT(5)
#define MCHP_INT15_ACPI_EC0_OBE BIT(6)
#define MCHP_INT15_ACPI_EC1_IBF BIT(7)
#define MCHP_INT15_ACPI_EC1_OBE BIT(8)
#define MCHP_INT15_ACPI_EC2_IBF BIT(9)
#define MCHP_INT15_ACPI_EC2_OBE BIT(10)
#define MCHP_INT15_ACPI_EC3_IBF BIT(11)
#define MCHP_INT15_ACPI_EC3_OBE BIT(12)
#define MCHP_INT15_ACPI_PM1_CTL BIT(15)
#define MCHP_INT15_ACPI_PM1_EN BIT(16)
#define MCHP_INT15_ACPI_PM1_STS BIT(17)
#define MCHP_INT15_8042_OBE BIT(18)
#define MCHP_INT15_8042_IBF BIT(19)
#define MCHP_INT15_MAILBOX BIT(20)
#define MCHP_INT15_P80_0 BIT(22)
#define MCHP_INT15_P80_1 BIT(23)
#define MCHP_INT15_P80(x) BIT(22 + ((x) & 0x01U))

/* GIRQ16 interrupt sources. Direct capable */
#define MCHP_INT16_PKE_ERR BIT(0)
#define MCHP_INT16_PKE_DONE BIT(1)
#define MCHP_INT16_RNG_DONE BIT(2)
#define MCHP_INT16_AES_DONE BIT(3)
#define MCHP_INT16_HASH_DONE BIT(4)

/* GIR17 interrupt sources. Direct capable */
#define MCHP_INT17_PECI BIT(0)
#define MCHP_INT17_TACH_0 BIT(1)
#define MCHP_INT17_TACH_1 BIT(2)
#define MCHP_INT17_TACH_2 BIT(3)
#define MCHP_INT17_TACH_3 BIT(4)
#define MCHP_INT17_HDMI_CEC BIT(5)
#define MCHP_INT17_ADC_SINGLE BIT(8)
#define MCHP_INT17_ADC_REPEAT BIT(9)
#define MCHP_INT17_LED_WDT_0 BIT(13)
#define MCHP_INT17_LED_WDT_1 BIT(14)
#define MCHP_INT17_LED_WDT_2 BIT(15)
#define MCHP_INT17_PROCHOT BIT(17)

/* GIRQ18 interrupt sources. Direct capable */
#define MCHP_INT18_SLV_SPI BIT(0)
#define MCHP_INT18_QMSPI BIT(1)
#define MCHP_INT18_PS2_0 BIT(10)
#define MCHP_INT18_PS2_1 BIT(11)
#define MCHP_INT18_CCT BIT(20)
#define MCHP_INT18_CCT_CAP0 BIT(21)
#define MCHP_INT18_CCT_CAP1 BIT(22)
#define MCHP_INT18_CCT_CAP2 BIT(23)
#define MCHP_INT18_CCT_CAP3 BIT(24)
#define MCHP_INT18_CCT_CAP4 BIT(25)
#define MCHP_INT18_CCT_CAP6 BIT(26)
#define MCHP_INT18_CCT_CMP0 BIT(27)
#define MCHP_INT18_CCT_CMP1 BIT(28)

/* GIRQ19 interrupt sources. Direct capable */
#define MCHP_INT19_ESPI_PC BIT(0)
#define MCHP_INT19_ESPI_BM1 BIT(1)
#define MCHP_INT19_ESPI_BM2 BIT(2)
#define MCHP_INT19_ESPI_LTR BIT(3)
#define MCHP_INT19_ESPI_OOB_TX BIT(4)
#define MCHP_INT19_ESPI_OOB_RX BIT(5)
#define MCHP_INT19_ESPI_FC BIT(6)
#define MCHP_INT19_ESPI_RESET BIT(7)
#define MCHP_INT19_ESPI_VW_EN BIT(8)
#define MCHP_INT19_ESPI_SAF BIT(9)
#define MCHP_INT19_ESPI_SAF_ERR BIT(10)

/* GIRQ20 interrupt sources. Direct capable */
#define MCHP_INT20_OPT BIT(3)

/* GIRQ21 interrupt sources. Direct capable */
#define MCHP_INT21_WDT BIT(2)
#define MCHP_INT21_WEEK_ALARM BIT(3)
#define MCHP_INT21_WEEK_SUB BIT(4)
#define MCHP_INT21_WEEK_1SEC BIT(5)
#define MCHP_INT21_WEEK_1SEC_SUB BIT(6)
#define MCHP_INT21_WEEK_PWR_PRES BIT(7)
#define MCHP_INT21_RTC BIT(8)
#define MCHP_INT21_RTC_ALARM BIT(9)
#define MCHP_INT21_VCI_OVRD BIT(10)
#define MCHP_INT21_VCI_IN0 BIT(11)
#define MCHP_INT21_VCI_IN1 BIT(12)
#define MCHP_INT21_VCI_IN2 BIT(13)
#define MCHP_INT21_VCI_IN3 BIT(14)
#define MCHP_INT21_PS2_0A_WAKE BIT(18)
#define MCHP_INT21_PS2_0B_WAKE BIT(19)
#define MCHP_INT21_PS2_1B_WAKE BIT(21)
#define MCHP_INT21_KEYSCAN BIT(25)

/* GIRQ22 peripheral wake only. GIRQ22 not connected to NVIC */
#define MCHP_INT22_WAKE_ONLY_SLV_SPI BIT(0)
#define MCHP_INT22_WAKE_ONLY_I2C0 BIT(1)
#define MCHP_INT22_WAKE_ONLY_I2C1 BIT(2)
#define MCHP_INT22_WAKE_ONLY_I2C2 BIT(3)
#define MCHP_INT22_WAKE_ONLY_I2C3 BIT(4)
#define MCHP_INT22_WAKE_ONLY_I2C4 BIT(5)
#define MCHP_INT22_WAKE_ONLY_I2C5 BIT(6)
#define MCHP_INT22_WAKE_ONLY_I2C6 BIT(7)
#define MCHP_INT22_WAKE_ONLY_I2C7 BIT(8)
#define MCHP_INT22_WAKE_ONLY_ESPI BIT(9)

/* GIRQ23 sources. Direct capable */
#define MCHP_INT23_BTMR16_0 BIT(0)
#define MCHP_INT23_BTMR16_1 BIT(1)
#define MCHP_INT23_BTMR32_0 BIT(4)
#define MCHP_INT23_BTMR32_1 BIT(5)
#define MCHP_INT23_RTMR BIT(10)
#define MCHP_INT23_HTMR_0 BIT(16)
#define MCHP_INT23_HTMR_1 BIT(17)

/* GIRQ24 sources. Master-to-Slave v=[0:6], Source=[0:3] */
#define MCHP_INT24_MSVW_SRC(v, s) (1ul << ((4 * (v)) + (s)))

/* GIRQ25 sources Master-to-Slave v=[7:10], Source=[0:3] */
#define MCHP_INT25_MSVW_SRC(v, s) (1ul << ((4 * ((v)-7)) + (s)))

/* UART Peripheral 0 <= x <= 2 */
#define MCHP_UART_INSTANCES 3
#define MCHP_UART_SPACING 0x400
#define MCHP_UART_CFG_OFS 0x300
#define MCHP_UART_CONFIG_BASE(x) \
	(MCHP_UART0_BASE + MCHP_UART_CFG_OFS + ((x) * MCHP_UART_SPACING))
#define MCHP_UART_RUNTIME_BASE(x) (MCHP_UART0_BASE + ((x) * MCHP_UART_SPACING))
#define MCHP_UART_GIRQ 15
#define MCHP_UART0_GIRQ_BIT (MCHP_INT15_UART_0)
#define MCHP_UART1_GIRQ_BIT (MCHP_INT15_UART_1)
#define MCHP_UART2_GIRQ_BIT (MCHP_INT15_UART_2)
#define MCHP_UART_GIRQ_BIT(x) BIT(x)
/* BIT defines for MCHP_UARTx_LSR */
#define MCHP_LSR_TX_EMPTY BIT(5)

/*
 * GPIO
 * MCHP each Port contains 32 GPIO's.
 * GPIO Control 1 registers are 32-bit registers starting at
 * MCHP_GPIO_BASE.
 * index = octal GPIO number from MCHP specification.
 * port/bank = index >> 5
 * id = index & 0x1F
 *
 * The port/bank, id pair may also be used to access GPIO's via
 * parallel I/O registers if GPIO control is configured for
 * parallel I/O.
 *
 * From ec/chip/mec1701/config_chip.h
 * #define GPIO_PIN(index) ((index) >> 5), ((index) & 0x1F)
 *
 * GPIO Control 1 Address = 0x40081000 + (((bank << 5) + id) << 2)
 *
 * Example: GPIO043, Control 1 register address = 0x4008108c
 * port/bank = 0x23 >> 5 = 1
 * id        = 0x23 & 0x1F = 0x03
 * Control 1 Address = 0x40081000 + ((BIT(5) + 0x03) << 2) = 0x4008108c
 *
 * Example: GPIO235, Control 1 register address = 0x40081274
 * port/bank = 0x9d >> 5   = 4
 * id        = 0x9d & 0x1f = 0x1d
 * Control 1 Address = 0x40081000 + (((4 << 5) + 0x1d) << 2) = 0x40081274
 */
#define MCHP_GPIO_CTL(port, id) \
	REG32(MCHP_GPIO_BASE + (((port << 5) + id) << 2))

/* MCHP implements 6 GPIO ports */
#define MCHP_GPIO_MAX_PORT 6
#define UNIMPLEMENTED_GPIO_BANK MCHP_GPIO_MAX_PORT

/*
 * In MECxxxx documentation GPIO numbers are octal, each control
 * register is located on a 32-bit boundary.
 */
#define MCHP_GPIO_CTRL(gpio_num) REG32(MCHP_GPIO_BASE + ((gpio_num) << 2))

/*
 * GPIO control register bit fields
 */
#define MCHP_GPIO_CTRL_PUD_BITPOS 0
#define MCHP_GPIO_CTRL_PUD_MASK0 0x03
#define MCHP_GPIO_CTRL_PUD_MASK 0x03
#define MCHP_GPIO_CTRL_PUD_NONE 0x00
#define MCHP_GPIO_CTRL_PUD_PU 0x01
#define MCHP_GPIO_CTRL_PUD_PD 0x02
#define MCHP_GPIO_CTRL_PUD_KEEPER 0x03
#define MCHP_GPIO_CTRL_PWR_BITPOS 2
#define MCHP_GPIO_CTRL_PWR_MASK0 0x03
#define MCHP_GPIO_CTRL_PWR_MASK (0x03 << 2)
#define MCHP_GPIO_CTRL_PWR_VTR (0x00 << 2)
#define MCHP_GPIO_CTRL_PWR_OFF (0x02 << 2)
#define MCHP_GPIO_INTDET_MASK 0xF0
#define MCHP_GPIO_INTDET_LVL_LO 0x00
#define MCHP_GPIO_INTDET_LVL_HI 0x10
#define MCHP_GPIO_INTDET_DISABLED 0x40
#define MCHP_GPIO_INTDET_EDGE_RIS 0xD0
#define MCHP_GPIO_INTDET_EDGE_FALL 0xE0
#define MCHP_GPIO_INTDET_EDGE_BOTH 0xF0
#define MCHP_GPIO_INTDET_EDGE_EN BIT(7)
#define MCHP_GPIO_PUSH_PULL 0u
#define MCHP_GPIO_OPEN_DRAIN BIT(8)
#define MCHP_GPIO_INPUT 0u
#define MCHP_GPIO_OUTPUT BIT(9)
#define MCHP_GPIO_OUTSET_CTRL 0u
#define MCHP_GPIO_OUTSEL_PAR BIT(10)
#define MCHP_GPIO_POLARITY_NINV 0u
#define MCHP_GPIO_POLARITY_INV BIT(11)
#define MCHP_GPIO_CTRL_ALT_FUNC_BITPOS 12
#define MCHP_GPIO_CTRL_ALT_FUNC_MASK0 0x0F
#define MCHP_GPIO_CTRL_ALT_FUNC_MASK (0x0F << 12)
#define MCHP_GPIO_CTRL_FUNC_GPIO (0 << 12)
#define MCHP_GPIO_CTRL_FUNC_1 (1 << 12)
#define MCHP_GPIO_CTRL_FUNC_2 (2 << 12)
#define MCHP_GPIO_CTRL_FUNC_3 (3 << 12)
#define MCHP_GPIO_CTRL_OUT_LVL BIT(16)
/* MEC15xx only */
#define MCHP_GPIO_CTRL_DIS_INPUT_BITPOS 15
#define MCHP_GPIO_CTRL_DIS_INPUT_BIT BIT(15)

/*
 * GPIO Parallel Input and Output registers.
 * gpio_bank in [0, 5]
 */
#define MCHP_GPIO_PARIN(bank) REG32(MCHP_GPIO_BASE + 0x0300 + ((bank) << 2))
#define MCHP_GPIO_PAROUT(bank) REG32(MCHP_GPIO_BASE + 0x0380 + ((bank) << 2))

/* Basic timers */
#define MCHP_TMR_SPACING 0x20
#define MCHP_TMR16_INSTANCES 2
#define MCHP_TMR32_INSTANCES 2
#define MCHP_TMR16_MAX (MCHP_TMR16_INSTANCES)
#define MCHP_TMR32_MAX (MCHP_TMR32_INSTANCES)
#define MCHP_TMR16_BASE(n) (MCHP_TMR16_0_BASE + (n) * MCHP_TMR_SPACING)
#define MCHP_TMR32_BASE(n) (MCHP_TMR32_0_BASE + (n) * MCHP_TMR_SPACING)
#define MCHP_TMR16_GIRQ 23
#define MCHP_TMR16_GIRQ_BIT(n) BIT(0 + (n))
#define MCHP_TMR32_GIRQ 23
#define MCHP_TMR32_GIRQ_BIT(n) BIT(4 + (n))

/* RTimer */
#define MCHP_RTMR_GIRQ 23
#define MCHP_RTMR_GIRQ_BIT(x) BIT(10)

/* Watchdog */
/* MEC152x specific registers */
#define MCHP_WDG_STATUS REG32(MCHP_WDG_BASE + 0x10)
#define MCHP_WDG_IEN REG32(MCHP_WDG_BASE + 0x14)
/* Status */
#define MCHP_WDG_STS_IRQ BIT(0)
/* Interrupt enable */
#define MCHP_WDG_IEN_IRQ_EN BIT(0)
#define MCHP_WDG_GIRQ 21
#define MCHP_WDG_GIRQ_BIT BIT(2)
/* Control register has a bit to enable IRQ generation */
#define MCHP_WDG_RESET_IRQ_EN BIT(9)

/* VBAT */
#define MCHP_VBAT_STS REG32(MCHP_VBAT_BASE + 0x0)
#define MCHP_VBAT_CE REG32(MCHP_VBAT_BASE + 0x8)
#define MCHP_VBAT_SHDN_DIS REG32(MCHP_VBAT_BASE + 0xC)
#define MCHP_VBAT_MONOTONIC_CTR_LO REG32(MCHP_VBAT_BASE + 0x20)
#define MCHP_VBAT_MONOTONIC_CTR_HI REG32(MCHP_VBAT_BASE + 0x24)
/* read 32-bit word at 32-bit offset x where 0 <= x <= 16 */
#define MCHP_VBAT_RAM_SIZE 64
#define MCHP_VBAT_RAM(wnum) REG32(MCHP_VBAT_RAM_BASE + ((wnum) * 4))
#define MCHP_VBAT_RAM8(bnum) REG8(MCHP_VBAT_RAM_BASE + (bnum))
#define MCHP_VBAT_VWIRE_BACKUP 14
/*
 * Miscellaneous firmware control fields
 * scratch pad index cannot be more than 32 as
 * MEC152x has 64 bytes = 16 words of scratch pad RAM
 */
#define MCHP_IMAGETYPE_IDX 15

/* Bit definition for MCHP_VBAT_STS */
#define MCHP_VBAT_STS_SOFTRESET BIT(2)
#define MCHP_VBAT_STS_RESETI BIT(4)
#define MCHP_VBAT_STS_WDT BIT(5)
#define MCHP_VBAT_STS_SYSRESETREQ BIT(6)
#define MCHP_VBAT_STS_VBAT_RST BIT(7)
#define MCHP_VBAT_STS_ANY_RST 0xF4u

/* Bit definitions for MCHP_VBAT_CE */
#define MCHP_VBAT_CE_XOSEL_BITPOS 3
#define MCHP_VBAT_CE_XOSEL_MASK BIT(3)
#define MCHP_VBAT_CE_XOSEL_PAR 0
#define MCHP_VBAT_CE_XOSEL_SE BIT(3)

#define MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_BITPOS 2
#define MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_MASK BIT(2)
#define MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_INT 0
#define MCHP_VBAT_CE_ALWAYS_ON_32K_SRC_CRYSTAL BIT(2)

#define MCHP_VBAT_CE_32K_DOMAIN_SRC_BITPOS 1
#define MCHP_VBAT_CE_32K_DOMAIN_SRC_MASK BIT(1)
#define MCHP_VBAT_CE_32K_DOMAIN_ALWAYS_ON 0
#define MCHP_VBAT_CE_32K_DOMAIN_32KHZ_IN_PIN BIT(1)

/* Blinking-Breathing LED 0 <= n <= 2 */
#define MCHP_BBLEB_INSTANCES 3
#define MCHP_BBLED_BASE(n) (MCHP_BBLED_0_BASE + (((n) & 0x03) * 256))

/* EMI */
#define MCHP_EMI_INSTANCES 2
#define MCHP_EMI_SPACING 0x400
#define MCHP_EMI_ECREG_OFS 0x100
/* base of EMI registers only accessible by EC */
#define MCHP_EMI_BASE(n) \
	(MCHP_EMI_0_BASE + MCHP_EMI_ECREG_OFS + ((n) * MCHP_EMI_SPACING))
/* base of EMI registers accessible by EC and Host */
#define MCHP_EMI_RT_BASE(n) (MCHP_EMI_0_BASE + ((n) * MCHP_EMI_SPACING))
#define MCHP_EMI_GIRQ 15
#define MCHP_EMI_GIRQ_BIT(n) BIT(2 + (n))

/* Mailbox */
#define MCHP_MBX_ECREGS_OFS 0x100
#define MCHP_MBX_RT_BASE MCHP_MBOX_BASE
#define MCHP_MBX_BASE (MCHP_MBOX_BASE + MCHP_MBX_ECREGS_OFS)
#define MCHP_MBX_GIRQ 15
#define MCHP_MBX_GIRQ_BIT BIT(20)

/* Port 80 Capture */
#define MCHP_P80_SPACING 0x400
#define MCHP_P80_BASE(n) (MCHP_P80CAP0_BASE + ((n) * (MCHP_P80_SPACING)))
#define MCHP_P80_HOST_DATA(n) REG8(MCHP_P80_BASE(n))
/* Data capture with time stamp register */
#define MCHP_P80_CAP(n) REG32(MCHP_P80_BASE(n) + 0x100)
#define MCHP_P80_CFG(n) REG8(MCHP_P80_BASE(n) + 0x104)
#define MCHP_P80_STS(n) REG8(MCHP_P80_BASE(n) + 0x108)
#define MCHP_P80_CNT(n) REG32(MCHP_P80_BASE(n) + 0x10c)
#define MCHP_P80_CNT_GET(n) (REG32(MCHP_P80_BASE(n) + 0x10c) >> 8)
#define MCHP_P80_CNT_SET(n, c) (REG32(MCHP_P80_BASE(n) + 0x10c) = ((c) << 8))
#define MCHP_P80_ACTIVATE(n) REG8(MCHP_P80_BASE(n) + 0x330)
#define MCHP_P80_GIRQ 15
#define MCHP_P80_GIRQ_BIT(n) BIT(22 + (n))
/*
 * Port 80 Data register bits
 * bits[7:0] = data captured on Host write
 * bits[31:8] = optional time stamp
 */
#define MCHP_P80_CAP_DATA_MASK 0xFFul
#define MCHP_P80_CAP_TS_BITPOS 8
#define MCHP_P80_CAP_TS_MASK0 0xfffffful
#define MCHP_P80_CAP_TS_MASK \
	((MCHP_P80_CAP_TS_MASK0) << (MCHP_P80_CAP_TS_BITPOS))

/* Port 80 Configuration register bits */
#define MCHP_P80_FLUSH_FIFO_WO BIT(1)
#define MCHP_P80_RESET_TIMESTAMP_WO BIT(2)
#define MCHP_P80_TIMEBASE_BITPOS 3
#define MCHP_P80_TIMEBASE_MASK0 0x03
#define MCHP_P80_TIMEBASE_MASK \
	((MCHP_P80_TIMEBASE_MASK0) << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_750KHZ (0x03 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_1500KHZ (0x02 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_3MHZ (0x01 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMEBASE_6MHZ (0x00 << (MCHP_P80_TIMEBASE_BITPOS))
#define MCHP_P80_TIMER_ENABLE BIT(5)
#define MCHP_P80_FIFO_THRHOLD_MASK (3u << 6)
#define MCHP_P80_FIFO_THRHOLD_1 0u
#define MCHP_P80_FIFO_THRHOLD_4 (1u << 6)
#define MCHP_P80_FIFO_THRHOLD_8 (2u << 6)
#define MCHP_P80_FIFO_THRHOLD_14 (3u << 6)
#define MCHP_P80_FIFO_LEN 16
/* Port 80 Status register bits, read-only */
#define MCHP_P80_STS_NOT_EMPTY BIT(0)
#define MCHP_P80_STS_OVERRUN BIT(1)
/* Port 80 Count register bits */
#define MCHP_P80_CNT_BITPOS 8
#define MCHP_P80_CNT_MASK0 0xfffffful
#define MCHP_P80_CNT_MASK ((MCHP_P80_CNT_MASK0) << (MCHP_P80_CNT_BITPOS))

/* PWM */
#define MCHP_PWM_INSTANCES 9
#define MCHP_PWM_ID_MAX (MCHP_PWM_INSTANCES)
#define MCHP_PWM_SPACING 16
#define MCHP_PWM_BASE(x) (MCHP_PWM_0_BASE + ((x) * MCHP_PWM_SPACING))

/* TACH */
#define MCHP_TACH_INSTANCES 4
#define MCHP_TACH_SPACING 16
#define MCHP_TACH_BASE(x) (MCHP_TACH_0_BASE + ((x) * MCHP_TACH_SPACING))
#define MCHP_TACH_GIRQ 17
#define MCHP_TACH_GIRQ_BIT(x) BIT(1 + (x))

/* ACPI EC */
#define MCHP_ACPI_EC_INSTANCES 4
#define MCHP_ACPI_EC_MAX (ACPI_EC_INSTANCES)
#define MCHP_ACPI_EC_SPACING 0x400
#define MCHP_ACPI_EC_BASE(x) \
	(MCHP_ACPI_EC_0_BASE + ((x) * MCHP_ACPI_EC_SPACING))
#define MCHP_ACPI_EC_GIRQ 15
#define MCHP_ACPI_EC_IBF_GIRQ_BIT(x) BIT(5 + ((x) * 2))
#define MCHP_ACPI_EC_OBE_GIRQ_BIT(x) BIT(6 + ((x) * 2))

/* ACPI PM1 */
#define MCHP_ACPI_PM1_ECREGS_OFS 0x100
#define MCHP_ACPI_PM_RT_BASE MCHP_ACPI_PM1_BASE
#define MCHP_ACPI_PM_EC_BASE (MCHP_ACPI_PM1_BASE + MCHP_ACPI_PM1_ECREGS_OFS)
#define MCHP_ACPI_PM1_CTL_GIRQ_BIT BIT(15)
#define MCHP_ACPI_PM1_EN_GIRQ_BIT BIT(16)
#define MCHP_ACPI_PM1_STS_GIRQ_BIT BIT(17)

/* 8042 */
#define MCHP_8042_ECREGS_OFS 0x100
#define MCHP_8042_GIRQ 15
#define MCHP_8042_OBE_GIRQ_BIT BIT(18)
#define MCHP_8042_IBF_GIRQ_BIT BIT(19)

/*
 * I2C controllers 0 - 4 include SMBus network layer functionality.
 * I2C controllers 5 - 7 are I2C only and include slave mode
 * promiscuous functionality.
 */
#define MCHP_I2C_CTRL0 0
#define MCHP_I2C_CTRL1 1
#define MCHP_I2C_CTRL2 2
#define MCHP_I2C_CTRL3 3
#define MCHP_I2C_CTRL4 4
#define MCHP_I2C_CTRL5 5
#define MCHP_I2C_CTRL6 6
#define MCHP_I2C_CTRL7 7
#define MCHP_I2C_CTRL_MAX 8

#define MCHP_I2C_SEP0 0x400
#define MCHP_I2C_SEP1 0x100

/*
 * MEC152xH 144-pin package has eight I2C controllers and sixteen ports.
 * Any port can be mapped to any I2C controller.
 *
 * I2C port values must be zero based consecutive whole numbers due to
 * port number used as an index for I2C mutex array, etc.
 *
 * Refer to chip i2c_port_to_controller function for mapping
 * of port to controller.
 *
 * Locking must occur by-controller (not by-port).
 * I2C00_SCL/SDA on GPIO004 F1, GPIO003 F1
 * I2C01_SCL/SDA on GPIO0131 F1, GPIO130 F1
 *	Alternate pins: GPIO073 F2, GPIO072 F2
 * I2C02_SCL/SDA on GPIO0155 F1, GPIO0154 F1
 * I2C03_SCL/SDA on GPIO010 F1, GPIO007 F1
 * I2C04_SCL/SDA on GPIO0144 F1, GPIO0143 F1
 * I2C05_SCL/SDA on GPIO0142 F1, GPIO0141 F1
 * I2C06_SCL/SDA on GPIO0140 F1, GPIO0132 F1
 * I2C07_SCL/SDA on GPIO013 F1, GPIO012 F1
 *	Alternate pins: GPIO0024 F3, GPIO0152 F3
 * I2C08_SCL/SDA on GPIO012 F1, GPIO0211 F1
 * I2C09_SCL/SDA on GPIO0146 F1, GPIO0145 F1
 * I2C10_SCL/SDA on GPIO0107 F3, GPIO030 F2
 * I2C11_SCL/SDA on GPIO062 F2, GPIO000 F3
 * I2C12_SCL/SDA on GPIO027 F3, GPIO026 F3
 * I2C13_SCL/SDA on GPIO065 F2, GPIO066 F2
 * I2C14_SCL/SDA on GPIO071 F2, GPIO070 F2
 * I2C15_SCL/SDA on GPIO0150 F1, GPIO0147 F1
 */

#define MCHP_MEC1521SZ_I2C_PORT_MASK 0xFEFFul
#define MCHP_MEC1523SZ_I2C_PORT_MASK 0xFFFFul

#define MCHP_I2C_PORT_MASK MCHP_MEC1521SZ_I2C_PORT_MASK

enum MCHP_i2c_port {
	MCHP_I2C_PORT0 = 0,
	MCHP_I2C_PORT1,
	MCHP_I2C_PORT2,
	MCHP_I2C_PORT3,
	MCHP_I2C_PORT4,
	MCHP_I2C_PORT5,
	MCHP_I2C_PORT6,
	MCHP_I2C_PORT7,
	MCHP_I2C_PORT8,
	MCHP_I2C_PORT9,
	MCHP_I2C_PORT10,
	MCHP_I2C_PORT11,
	MCHP_I2C_PORT12,
	MCHP_I2C_PORT13,
	MCHP_I2C_PORT14,
	MCHP_I2C_PORT15,
	MCHP_I2C_PORT_COUNT,
};

/* I2C ports & Configs */
#define I2C_CONTROLLER_COUNT MCHP_I2C_CTRL_MAX
#define I2C_PORT_COUNT MCHP_I2C_PORT_COUNT

/*
 * I2C controllers 0-4 implement network layer hardware.
 * I2C controllers 5-7 do include network layer hardware.
 * MEC152x has I2C promiscuous mode feature in the following
 * additional registers.
 */
#define MCHP_I2C_SLAVE_ADDR(ctrl) REG32(MCHP_I2C_ADDR(ctrl, 0x6c))
#define MCHP_I2C_PROM_INTR(ctrl) REG32(MCHP_I2C_ADDR(ctrl, 0x70))
#define MCHP_I2C_PROM_INTR_EN(ctrl) REG32(MCHP_I2C_ADDR(ctrl, 0x74))
#define MCHP_I2C_PROM_CTRL(ctrl) REG32(MCHP_I2C_ADDR(ctrl, 0x78))

/* All I2C controllers connected to GIRQ13 */
#define MCHP_I2C_GIRQ 13
/* I2C[0:7] -> GIRQ13 bits[0:7] */
#define MCHP_I2C_GIRQ_BIT(n) BIT((n))

/* Keyboard scan matrix */
#define MCHP_KS_GIRQ 21
#define MCHP_KS_GIRQ_BIT BIT(25)
#define MCHP_KS_DIRECT_NVIC 135

/* ADC */
#define MCHP_ADC_GIRQ 17
#define MCHP_ADC_GIRQ_SINGLE_BIT BIT(8)
#define MCHP_ADC_GIRQ_REPEAT_BIT BIT(9)
#define MCHP_ADC_SINGLE_DIRECT_NVIC 78
#define MCHP_ADC_REPEAT_DIRECT_NVIC 79

/* Hibernation timer */
#define MCHP_HTIMER_SPACING 0x20
#define MCHP_HTIMER_ADDR(n) (MCHP_HTIMER_BASE + ((n) * MCHP_HTIMER_SPACING))
#define MCHP_HTIMER_GIRQ 23
/* HTIMER[0:1] -> GIRQ23 bits[16:17] */
#define MCHP_HTIMER_GIRQ_BIT(n) BIT(16 + (n))
#define MCHP_HTIMER_DIRECT_NVIC(n) (112 + (n))

/*
 * Quad Master SPI (QMSPI)
 * MEC152x implements 16 descriptors, support for two chip selects,
 * and additional SPI signal timing registers.
 */
#define MCHP_QMSPI_MAX_DESCR 16
/*
 * Chip select implemented in bit[13:12] of the Mode register.
 * These bits are reserved in earlier chips.
 */
#define MCHP_QMSPI_M_CS_POS 12
#define MCHP_QMSPI_M_CS_MASK0 0x03
#define MCHP_QMSPI_M_CS_MASK (0x03 << MCHP_QMSPI_M_CS_POS)
#define MCHP_QMSPI_M_CS0 (0x00 << MCHP_QMSPI_M_CS_POS)
#define MCHP_QMSPI_M_CS1 (0x01 << MCHP_QMSPI_M_CS_POS)

/* New QMSPI chip select timing register */
#define MCHP_QMSPI_CS_TIMING REG32(MCHP_QMSPI0_BASE + 0x28)
#define MCHP_QMSPI_CST_DFLT_VAL 0x06060406
#define MCHP_QMSPI_CST_ON2CLK_MASK 0x0f
#define MCHP_QMSPI_CST_ON2CLK_DFLT 0x06
#define MCHP_QMSPI_CST_DLY_CLK2OFF_POS 8
#define MCHP_QMSPI_CST_DLY_CLK2OFF_MASK0 0x0f
#define MCHP_QMSPI_CST_DLY_CLK2OFF_MASK 0x0f00
#define MCHP_QMSPI_CST_DLY_CLK2OFF_DFLT 0x0400
#define MCHP_QMSPI_CST_DLY_LDH_POS 16
#define MCHP_QMSPI_CST_DLY_LDH_MASK0 0x0f
#define MCHP_QMSPI_CST_DLY_LDH_MASK 0xf0000
#define MCHP_QMSPI_CST_DLY_LDH_DFLT 0x60000
#define MCHP_QMSPI_CST_DLY_OFF2ON_POS 24
#define MCHP_QMSPI_CST_DLY_OFF2ON_DFLT 0x06000000
#define MCHP_QMSPI_CST_DLY_OFF2ON_MASK0 0xff
#define MCHP_QMSPI_CST_DLY_OFF2ON_MASK 0xff000000

#define MCHP_QMSPI_GIRQ 18
#define MCHP_QMSPI_GIRQ_BIT BIT(1)
#define MCHP_QMSPI_DIRECT_NVIC 91

/* eSPI */

/* IO BAR defines. Use with MCHP_ESPI_IO_BAR_xxxx macros */
#define MCHP_ESPI_IO_BAR_ID_CFG_PORT 0
#define MCHP_ESPI_IO_BAR_ID_MEM_CMPNT 1
#define MCHP_ESPI_IO_BAR_ID_MAILBOX 2
#define MCHP_ESPI_IO_BAR_ID_8042 3
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC0 4
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC1 5
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC2 6
#define MCHP_ESPI_IO_BAR_ID_ACPI_EC3 7
#define MCHP_ESPI_IO_BAR_ID_ACPI_PM1 9
#define MCHP_ESPI_IO_BAR_ID_P92 0xA
#define MCHP_ESPI_IO_BAR_ID_UART0 0xB
#define MCHP_ESPI_IO_BAR_ID_UART1 0xC
#define MCHP_ESPI_IO_BAR_ID_EMI0 0xD
#define MCHP_ESPI_IO_BAR_ID_EMI1 0xE
#define MCHP_ESPI_IO_BAR_P80_0 0x10
#define MCHP_ESPI_IO_BAR_P80_1 0x11
#define MCHP_ESPI_IO_BAR_RTC 0x12
#define MCHP_ESPI_IO_BAR_ID_UART2 0x15

/* Use with MCHP_ESPI_MBAR_EC_xxxx(x) macros */
#define MCHP_ESPI_MBAR_ID_MBOX 0
#define MCHP_ESPI_MBAR_ID_ACPI_EC_0 1
#define MCHP_ESPI_MBAR_ID_ACPI_EC_1 2
#define MCHP_ESPI_MBAR_ID_ACPI_EC_2 3
#define MCHP_ESPI_MBAR_ID_ACPI_EC_3 4
#define MCHP_ESPI_MBAR_ID_EMI_0 6
#define MCHP_ESPI_MBAR_ID_EMI_1 7

/* Use with MCHP_ESPI_IO_SERIRQ_REG(x) */
#define MCHP_ESPI_SIRQ_MBOX 0 /* Host SIRQ */
#define MCHP_ESPI_SIRQ_MBOX_SMI 1 /* Host SMI */
#define MCHP_ESPI_SIRQ_8042_KB 2 /* KIRQ */
#define MCHP_ESPI_SIRQ_8042_MS 3 /* MIRQ */
#define MCHP_ESPI_SIRQ_ACPI_EC0_OBF 4
#define MCHP_ESPI_SIRQ_ACPI_EC1_OBF 5
#define MCHP_ESPI_SIRQ_ACPI_EC2_OBF 6
#define MCHP_ESPI_SIRQ_ACPI_EC3_OBF 7
#define MCHP_ESPI_SIRQ_UART0 9
#define MCHP_ESPI_SIRQ_UART1 10
#define MCHP_ESPI_SIRQ_EMI0_HEV 11 /* Host Event */
#define MCHP_ESPI_SIRQ_EMI0_EC2H 12 /* EC to Host */
#define MCHP_ESPI_SIRQ_EMI1_HEV 13
#define MCHP_ESPI_SIRQ_EMI1_EC2H 14
#define MCHP_ESPI_SIRQ_RTC 17
#define MCHP_ESPI_SIRQ_EC 18
#define MCHP_ESPI_SIRQ_UART2 19

#define MCHP_ESPI_MSVW_BASE (MCHP_ESPI_VW_BASE)
#define MCHP_ESPI_SMVW_BASE ((MCHP_ESPI_VW_BASE) + 0x200ul)

/*
 * eSPI RESET, channel enables and operations except Master-to-Slave
 * WWires are all on GIRQ19
 */
#define MCHP_ESPI_GIRQ 19
#define MCHP_ESPI_PC_GIRQ_BIT BIT(0)
#define MCHP_ESPI_BM1_GIRQ_BIT BIT(1)
#define MCHP_ESPI_BM2_GIRQ_BIT BIT(2)
#define MCHP_ESPI_LTR_GIRQ_BIT BIT(3)
#define MCHP_ESPI_OOB_TX_GIRQ_BIT BIT(4)
#define MCHP_ESPI_OOB_RX_GIRQ_BIT BIT(5)
#define MCHP_ESPI_FC_GIRQ_BIT BIT(6)
#define MCHP_ESPI_RESET_GIRQ_BIT BIT(7)
#define MCHP_ESPI_VW_EN_GIRQ_BIT BIT(8)
#define MCHP_ESPI_SAF_DONE_GIRQ_BIT BIT(9)
#define MCHP_ESPI_SAF_ERR_GIRQ_BIT BIT(10)

/*
 * eSPI Master-to-Slave WWire interrupts are on GIRQ24 and GIRQ25
 */
#define MCHP_ESPI_MSVW_0_6_GIRQ 24
#define MCHP_ESPI_MSVW_7_10_GIRQ 25
/*
 * Four source bits, SRC[0:3] per Master-to-Slave register
 * v = MSVW [0:10]
 * n = VWire SRC bit = [0:3]
 */
#define MCHP_ESPI_MSVW_GIRQ(v) (24 + ((v) > 6 ? 1 : 0))

#define MCHP_ESPI_MSVW_SRC_GIRQ_BIT(v, n) \
	(((v) > 6) ? (1ul << (((v)-7) + (n))) : (1ul << ((v) + (n))))

/* DMA */
#define MCHP_DMA_MAX_CHAN 12
#define MCHP_DMA_CH_OFS 0x40
#define MCHP_DMA_CH_OFS_BITPOS 6
#define MCHP_DMA_CH_BASE (MCHP_DMA_BASE + MCHP_DMA_CH_OFS)

/*
 * Available DMA channels.
 * On MCHP, any DMA channel may serve any device. Since we have
 * 12 channels and 12 devices request signals, we make each channel
 * dedicated to the device of the same number.
 */
enum dma_channel {
	/* Channel numbers */
	MCHP_DMAC_I2C0_SLAVE = 0,
	MCHP_DMAC_I2C0_MASTER,
	MCHP_DMAC_I2C1_SLAVE,
	MCHP_DMAC_I2C1_MASTER,
	MCHP_DMAC_I2C2_SLAVE,
	MCHP_DMAC_I2C2_MASTER,
	MCHP_DMAC_I2C3_SLAVE,
	MCHP_DMAC_I2C3_MASTER,
	MCHP_DMAC_I2C4_SLAVE,
	MCHP_DMAC_I2C4_MASTER,
	MCHP_DMAC_QMSPI0_TX,
	MCHP_DMAC_QMSPI0_RX,
	/* Channel count */
	MCHP_DMAC_COUNT,
};

/*
 * Peripheral device DMA Device ID's for bits [15:9]
 * in DMA channel control register.
 */
#define MCHP_DMA_I2C0_SLV_REQ_ID 0
#define MCHP_DMA_I2C0_MTR_REQ_ID 1
#define MCHP_DMA_I2C1_SLV_REQ_ID 2
#define MCHP_DMA_I2C1_MTR_REQ_ID 3
#define MCHP_DMA_I2C2_SLV_REQ_ID 4
#define MCHP_DMA_I2C2_MTR_REQ_ID 5
#define MCHP_DMA_I2C3_SLV_REQ_ID 6
#define MCHP_DMA_I2C3_MTR_REQ_ID 7
#define MCHP_DMA_I2C4_SLV_REQ_ID 8
#define MCHP_DMA_I2C4_MTR_REQ_ID 9
#define MCHP_DMA_QMSPI0_TX_REQ_ID 10
#define MCHP_DMA_QMSPI0_RX_REQ_ID 11

/*
 * Hardware delay register.
 * Write of 0 <= n <= 31 will stall the Cortex-M4
 * for n+1 microseconds. Interrupts will not be
 * serviced during the delay period. Reads have
 * no effect.
 */
#define MCHP_USEC_DELAY_REG_ADDR 0x10000000
#define MCHP_USEC_DELAY(x) (REG8(MCHP_USEC_DELAY_REG_ADDR) = (x))

#endif /* #ifndef __ASSEMBLER__ */
