/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for LM4x processor
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"

#define LM4_UART_CH0_BASE      0x4000c000
#define LM4_UART_CH1_BASE      0x4000d000
#define LM4_UART_CH_SEP        0x00001000
static inline int lm4_uart_addr(int ch, int offset)
{
	return offset + LM4_UART_CH0_BASE + LM4_UART_CH_SEP * ch;
}
#define LM4UARTREG(ch, offset) REG32(lm4_uart_addr(ch, offset))
#define LM4_UART_DR(ch)        LM4UARTREG(ch, 0x000)
#define LM4_UART_FR(ch)        LM4UARTREG(ch, 0x018)
#define LM4_UART_IBRD(ch)      LM4UARTREG(ch, 0x024)
#define LM4_UART_FBRD(ch)      LM4UARTREG(ch, 0x028)
#define LM4_UART_LCRH(ch)      LM4UARTREG(ch, 0x02c)
#define LM4_UART_CTL(ch)       LM4UARTREG(ch, 0x030)
#define LM4_UART_IFLS(ch)      LM4UARTREG(ch, 0x034)
#define LM4_UART_IM(ch)        LM4UARTREG(ch, 0x038)
#define LM4_UART_ICR(ch)       LM4UARTREG(ch, 0x044)
#define LM4_UART_DMACTL(ch)    LM4UARTREG(ch, 0x048)
#define LM4_UART_CC(ch)        LM4UARTREG(ch, 0xfc8)

#define LM4_SSI_BASE           0x40008000
#define LM4_SSI_CH_SEP         0x40001000
static inline int lm4_spi_addr(int ch, int offset)
{
	return offset + LM4_SSI_BASE + LM4_SSI_CH_SEP * ch;
}
#define LM4SSIREG(ch, offset)  REG32(lm4_spi_addr(ch, offset))
#define LM4_SSI_CR0(ch)        LM4SSIREG(ch, 0x000)
#define LM4_SSI_CR1(ch)        LM4SSIREG(ch, 0x004)
#define LM4_SSI_DR(ch)         LM4SSIREG(ch, 0x008)
#define LM4_SSI_SR(ch)         LM4SSIREG(ch, 0x00c)
#define LM4_SSI_SR_TFE         (1 << 0)  /* Transmit FIFO empty */
#define LM4_SSI_SR_TNF         (1 << 1)  /* Transmit FIFO not full */
#define LM4_SSI_SR_RNE         (1 << 2)  /* Receive FIFO not empty */
#define LM4_SSI_SR_RFF         (1 << 3)  /* Receive FIFO full */
#define LM4_SSI_SR_BSY         (1 << 4)  /* Busy */
#define LM4_SSI_CPSR(ch)       LM4SSIREG(ch, 0x010)
#define LM4_SSI_IM(ch)         LM4SSIREG(ch, 0x014)
#define LM4_SSI_RIS(ch)        LM4SSIREG(ch, 0x018)
#define LM4_SSI_MIS(ch)        LM4SSIREG(ch, 0x01c)
#define LM4_SSI_ICR(ch)        LM4SSIREG(ch, 0x020)
#define LM4_SSI_DMACTL(ch)     LM4SSIREG(ch, 0x024)
#define LM4_SSI_CC(ch)         LM4SSIREG(ch, 0xfc8)

#define LM4_ADC_ADCACTSS       REG32(0x40038000)
#define LM4_ADC_ADCRIS         REG32(0x40038004)
#define LM4_ADC_ADCIM          REG32(0x40038008)
#define LM4_ADC_ADCISC         REG32(0x4003800c)
#define LM4_ADC_ADCOSTAT       REG32(0x40038010)
#define LM4_ADC_ADCEMUX        REG32(0x40038014)
#define LM4_ADC_ADCUSTAT       REG32(0x40038018)
#define LM4_ADC_ADCSSPRI       REG32(0x40038020)
#define LM4_ADC_ADCSPC         REG32(0x40038024)
#define LM4_ADC_ADCPSSI        REG32(0x40038028)
#define LM4_ADC_ADCSAC         REG32(0x40038030)
#define LM4_ADC_ADCCTL         REG32(0x40038038)
#define LM4_ADC_ADCCC          REG32(0x40038fc8)
#define LM4_ADC_SS0_BASE       0x40038040
#define LM4_ADC_SS1_BASE       0x40038060
#define LM4_ADC_SS2_BASE       0x40038080
#define LM4_ADC_SS3_BASE       0x400380a0
#define LM4_ADC_SS_SEP         0x00000020
static inline int lm4_adc_addr(int ss, int offset)
{
	return offset + LM4_ADC_SS0_BASE + LM4_ADC_SS_SEP * ss;
}
#define LM4ADCREG(ss, offset)  REG32(lm4_adc_addr(ss, offset))
#define LM4_ADC_SSMUX(ss)      LM4ADCREG(ss, 0x000)
#define LM4_ADC_SSCTL(ss)      LM4ADCREG(ss, 0x004)
#define LM4_ADC_SSFIFO(ss)     LM4ADCREG(ss, 0x008)
#define LM4_ADC_SSFSTAT(ss)    LM4ADCREG(ss, 0x00c)
#define LM4_ADC_SSOP(ss)       LM4ADCREG(ss, 0x010)
#define LM4_ADC_SSEMUX(ss)     LM4ADCREG(ss, 0x018)

#define LM4_LPC_LPCCTL         REG32(0x40080000)
#define LM4_LPC_SCI_START      (1 << 9)  /* Start a pulse on LPC0SCI signal */
#define LM4_LPC_SCI_CLK_1      (0 << 10) /* SCI asserted for 1 clock period */
#define LM4_LPC_SCI_CLK_2      (1 << 10) /* SCI asserted for 2 clock periods */
#define LM4_LPC_SCI_CLK_4      (2 << 10) /* SCI asserted for 4 clock periods */
#define LM4_LPC_SCI_CLK_8      (3 << 10) /* SCI asserted for 8 clock periods */
#define LM4_LPC_LPCSTS         REG32(0x40080004)
#define LM4_LPC_LPCIRQCTL      REG32(0x40080008)
#define LM4_LPC_LPCIRQST       REG32(0x4008000c)
#define LM4_LPC_LPCIM          REG32(0x40080100)
#define LM4_LPC_LPCRIS         REG32(0x40080104)
#define LM4_LPC_LPCMIS         REG32(0x40080108)
#define LM4_LPC_LPCIC          REG32(0x4008010c)
#define LM4_LPC_INT_MASK(ch, bits) ((bits) << (4 * (ch)))
#define LM4_LPC_LPCDMACX       REG32(0x40080120)
#define LM4_LPC_CH0_BASE       0x40080010
#define LM4_LPC_CH1_BASE       0x40080020
#define LM4_LPC_CH2_BASE       0x40080030
#define LM4_LPC_CH3_BASE       0x40080040
#define LM4_LPC_CH4_BASE       0x40080050
#define LM4_LPC_CH5_BASE       0x40080060
#define LM4_LPC_CH6_BASE       0x40080070
#define LM4_LPC_CH7_BASE       0x40080080
#define LM4_LPC_CH_SEP         0x00000010
static inline int lm4_lpc_addr(int ch, int offset)
{
	return offset + LM4_LPC_CH0_BASE + LM4_LPC_CH_SEP * ch;
}
#define LM4LPCREG(ch, offset)  REG32(lm4_lpc_addr(ch, offset))
#define LM4_LPC_CTL(ch)        LM4LPCREG(ch, 0x000)
#define LM4_LPC_ST(ch)         LM4LPCREG(ch, 0x004)
#define LM4_LPC_ST_TOH         (1 << 0)  /* TO Host bit */
#define LM4_LPC_ST_FRMH        (1 << 1)  /* FRoM Host bit */
#define LM4_LPC_ST_CMD         (1 << 3)  /* Last from-host byte was command */
#define LM4_LPC_ST_PRESENT     (1 << 8)
#define LM4_LPC_ST_SCI         (1 << 9)
#define LM4_LPC_ST_SMI         (1 << 10)
#define LM4_LPC_ST_BUSY        (1 << 12)
#define LM4_LPC_ADR(ch)        LM4LPCREG(ch, 0x008)
#define LM4_LPC_POOL_BYTES     1024   /* Size of LPCPOOL in bytes */
#define LM4_LPC_LPCPOOL        ((volatile unsigned char *)0x40080400)

#define LM4_FAN_FANSTS         REG32(0x40084000)
#define LM4_FAN_FANCTL         REG32(0x40084004)
#define LM4_FAN_CH0_BASE       0x40084010
#define LM4_FAN_CH1_BASE       0x40084020
#define LM4_FAN_CH2_BASE       0x40084030
#define LM4_FAN_CH3_BASE       0x40084040
#define LM4_FAN_CH4_BASE       0x40084050
#define LM4_FAN_CH5_BASE       0x40084060
#define LM4_FAN_CH_SEP         0x00000010
static inline int lm4_fan_addr(int ch, int offset)
{
	return offset + LM4_FAN_CH0_BASE + LM4_FAN_CH_SEP * ch;
}
#define LM4FANREG(ch, offset)  REG32(lm4_fan_addr(ch, offset))
#define LM4_FAN_FANCH(ch)      LM4FANREG(ch, 0x000)
#define LM4_FAN_FANCMD(ch)     LM4FANREG(ch, 0x004)
#define LM4_FAN_FANCST(ch)     LM4FANREG(ch, 0x008)

#define LM4_EEPROM_EESIZE      REG32(0x400af000)
#define LM4_EEPROM_EEBLOCK     REG32(0x400af004)
#define LM4_EEPROM_EEOFFSET    REG32(0x400af008)
#define LM4_EEPROM_EERDWR      REG32(0x400af010)
#define LM4_EEPROM_EERDWRINC   REG32(0x400af014)
#define LM4_EEPROM_EEDONE      REG32(0x400af018)
#define LM4_EEPROM_EESUPP      REG32(0x400af01c)
#define LM4_EEPROM_EEUNLOCK    REG32(0x400af020)
#define LM4_EEPROM_EEPROT      REG32(0x400af030)
#define LM4_EEPROM_EEPASS0     REG32(0x400af034)
#define LM4_EEPROM_EEPASS1     REG32(0x400af038)
#define LM4_EEPROM_EEPASS2     REG32(0x400af03c)
#define LM4_EEPROM_EEINT       REG32(0x400af040)
#define LM4_EEPROM_EEHIDE      REG32(0x400af050)

#define LM4_PECI_CTL           REG32(0x400b0000)
#define LM4_PECI_DIV           REG32(0x400b0004)
#define LM4_PECI_CMP           REG32(0x400b0008)
#define LM4_PECI_M0D0C         REG32(0x400b0010)
#define LM4_PECI_M0D1C         REG32(0x400b0014)
#define LM4_PECI_M1D0C         REG32(0x400b0018)
#define LM4_PECI_M1D1C         REG32(0x400b001c)
#define LM4_PECI_M0D0          REG32(0x400b0040)
#define LM4_PECI_M0D1          REG32(0x400b0044)
#define LM4_PECI_M1D0          REG32(0x400b0048)
#define LM4_PECI_M1D1          REG32(0x400b004c)
#define LM4_PECI_IM            REG32(0x400b0080)
#define LM4_PECI_RIS           REG32(0x400b0084)
#define LM4_PECI_MIS           REG32(0x400b0088)
#define LM4_PECI_IC            REG32(0x400b008c)
#define LM4_PECI_ACADDR        REG32(0x400b0100)
#define LM4_PECI_ACARG         REG32(0x400b0104)
#define LM4_PECI_ACRDWR0       REG32(0x400b0108)
#define LM4_PECI_ACRDWR1       REG32(0x400b010c)
#define LM4_PECI_ACCMD         REG32(0x400b0110)
#define LM4_PECI_ACCODE        REG32(0x400b0114)


#define LM4_HIBERNATE_HIBRTCC  REG32(0x400fc000)
#define LM4_HIBERNATE_HIBRTCM0 REG32(0x400fc004)
#define LM4_HIBERNATE_HIBRTCLD REG32(0x400fc00c)
#define LM4_HIBERNATE_HIBCTL   REG32(0x400fc010)
#define LM4_HIBCTL_WRC         (1 << 31)
#define LM4_HIBCTL_CLK32EN     (1 << 6)
#define LM4_HIBCTL_PINWEN      (1 << 4)
#define LM4_HIBCTL_RTCWEN      (1 << 3)
#define LM4_HIBCTL_HIBREQ      (1 << 1)
#define LM4_HIBCTL_RTCEN       (1 << 0)
#define LM4_HIBERNATE_HIBIM    REG32(0x400fc014)
#define LM4_HIBERNATE_HIBRIS   REG32(0x400fc018)
#define LM4_HIBERNATE_HIBIC    REG32(0x400fc020)
#define LM4_HIBERNATE_HIBRTCT  REG32(0x400fc024)
#define LM4_HIBERNATE_HIBRTCSS REG32(0x400fc028)
#define LM4_HIBERNATE_HIBDATA_ENTRIES 16  /* Number of entries in HIBDATA[] */
#define LM4_HIBERNATE_HIBDATA  ((volatile uint32_t *)0x400fc030)

#define LM4_FLASH_FMA          REG32(0x400fd000)
#define LM4_FLASH_FMD          REG32(0x400fd004)
#define LM4_FLASH_FMC          REG32(0x400fd008)
#define LM4_FLASH_FCRIS        REG32(0x400fd00c)
#define LM4_FLASH_FCMISC       REG32(0x400fd014)
#define LM4_FLASH_FMC2         REG32(0x400fd020)
#define LM4_FLASH_FWBVAL       REG32(0x400fd030)
/* FWB size is 32 words = 128 bytes */
#define LM4_FLASH_FWB          ((volatile uint32_t*)0x400fd100)
#define LM4_FLASH_FSIZE        REG32(0x400fdfc0)
#define LM4_FLASH_FMPRE0       REG32(0x400fe200)
#define LM4_FLASH_FMPRE1       REG32(0x400fe204)
#define LM4_FLASH_FMPRE2       REG32(0x400fe208)
#define LM4_FLASH_FMPRE3       REG32(0x400fe20c)
#define LM4_FLASH_FMPPE        ((volatile uint32_t*)0x400fe400)
#define LM4_FLASH_FMPPE0       REG32(0x400fe400)
#define LM4_FLASH_FMPPE1       REG32(0x400fe404)
#define LM4_FLASH_FMPPE2       REG32(0x400fe408)
#define LM4_FLASH_FMPPE3       REG32(0x400fe40c)

#define LM4_SYSTEM_DID0        REG32(0x400fe000)
#define LM4_SYSTEM_DID1        REG32(0x400fe004)
#define LM4_SYSTEM_PBORCTL     REG32(0x400fe030)
#define LM4_SYSTEM_RIS         REG32(0x400fe050)
#define LM4_SYSTEM_MISC        REG32(0x400fe058)
#define LM4_SYSTEM_RESC        REG32(0x400fe05c)
#define LM4_SYSTEM_RCC         REG32(0x400fe060)
#define LM4_SYSTEM_RCC_SYSDIV(x)  (((x) & 0xf) << 23)
#define LM4_SYSTEM_RCC_USESYSDIV  (1 << 22)
#define LM4_SYSTEM_RCC_PWRDN      (1 << 13)
#define LM4_SYSTEM_RCC_BYPASS     (1 << 11)
#define LM4_SYSTEM_RCC_XTAL(x)    (((x) & 0x1f) << 6)
#define LM4_SYSTEM_RCC_OSCSRC(x)  (((x) & 0x3) << 4)
#define LM4_SYSTEM_RCC_IOSCDIS    (1 << 1)
#define LM4_SYSTEM_RCC_MOSCDIS    (1 << 0)
#define LM4_SYSTEM_RCC2        REG32(0x400fe070)
#define LM4_SYSTEM_RCC2_USERCC2     (1 << 31)
#define LM4_SYSTEM_RCC2_DIV400      (1 << 30)
#define LM4_SYSTEM_RCC2_SYSDIV2(x)  (((x) & 0x3f) << 23)
#define LM4_SYSTEM_RCC2_SYSDIV2LSB  (1 << 22)
#define LM4_SYSTEM_RCC2_PWRDN2      (1 << 13)
#define LM4_SYSTEM_RCC2_BYPASS2     (1 << 11)
#define LM4_SYSTEM_RCC2_OSCSRC2(x)  (((x) & 0x7) << 4)
#define LM4_SYSTEM_MOSCCTL     REG32(0x400fe07c)
#define LM4_SYSTEM_PIOSCCAL    REG32(0x400fe150)
#define LM4_SYSTEM_PIOSCSTAT   REG32(0x400fe154)
#define LM4_SYSTEM_PLLSTAT     REG32(0x400fe168)
#define LM4_SYSTEM_BOOTCFG     REG32(0x400fe1d0)
#define LM4_SYSTEM_BOOTCFG_MASK 0x7fff00ec /* Reserved bits of BOOTCFG reg */
/* Note: USER_REG3 is used to hold pre-programming process data and should not
 * be modified by EC code.  See crosbug.com/p/8889. */
#define LM4_SYSTEM_USER_REG3   REG32(0x400fe1ec)
#define LM4_SYSTEM_SRI2C       REG32(0x400fe520)
#define LM4_SYSTEM_SREEPROM    REG32(0x400fe558)

#define LM4_SYSTEM_RCGC_BASE   ((volatile uint32_t *)0x400fe600)
#define LM4_SYSTEM_RCGCGPIO    REG32(0x400fe608)
#define LM4_SYSTEM_SCGC_BASE   ((volatile uint32_t *)0x400fe700)
#define LM4_SYSTEM_DCGC_BASE   ((volatile uint32_t *)0x400fe800)

/*
 * Offsets from CGC_BASE registers for each peripheral.
 * Note: these are in units of 32-bit words offset from
 * the base address.
 */
enum clock_gate_offsets {
	CGC_OFFSET_WD =        0,
	CGC_OFFSET_TIMER =     1,
	CGC_OFFSET_GPIO =      2,
	CGC_OFFSET_DMA =       3,
	CGC_OFFSET_HIB =       5,
	CGC_OFFSET_UART =      6,
	CGC_OFFSET_SSI =       7,
	CGC_OFFSET_I2C =       8,
	CGC_OFFSET_ADC =       14,
	CGC_OFFSET_LPC =       18,
	CGC_OFFSET_PECI =      20,
	CGC_OFFSET_FAN =       21,
	CGC_OFFSET_EEPROM =    22,
	CGC_OFFSET_WTIMER =    23,
};

#define LM4_SYSTEM_PREEPROM    REG32(0x400fea58)

#define LM4_DMA_DMACFG         REG32(0x400ff004)
#define LM4_DMA_DMACTLBASE     REG32(0x400ff008)
#define LM4_DMA_DMACHMAP0      REG32(0x400ff510)
#define LM4_DMA_DMACHMAP1      REG32(0x400ff514)
#define LM4_DMA_DMACHMAP2      REG32(0x400ff518)
#define LM4_DMA_DMACHMAP3      REG32(0x400ff51c)

/* IRQ numbers */
#define LM4_IRQ_GPIOA            0
#define LM4_IRQ_GPIOB            1
#define LM4_IRQ_GPIOC            2
#define LM4_IRQ_GPIOD            3
#define LM4_IRQ_GPIOE            4
#define LM4_IRQ_UART0            5
#define LM4_IRQ_UART1            6
#define LM4_IRQ_SSI0             7
#define LM4_IRQ_I2C0             8
/* 9 - 13 reserved */
#define LM4_IRQ_ADC0_SS0        14
#define LM4_IRQ_ADC0_SS1        15
#define LM4_IRQ_ADC0_SS2        16
#define LM4_IRQ_ADC0_SS3        17
#define LM4_IRQ_WATCHDOG        18
#define LM4_IRQ_TIMER0A         19
#define LM4_IRQ_TIMER0B         20
#define LM4_IRQ_TIMER1A         21
#define LM4_IRQ_TIMER1B         22
#define LM4_IRQ_TIMER2A         23
#define LM4_IRQ_TIMER2B         24
#define LM4_IRQ_ACMP0           25
#define LM4_IRQ_ACMP1           26
#define LM4_IRQ_ACMP2           27
#define LM4_IRQ_SYSCTRL         28
#define LM4_IRQ_EEPROM          29
#define LM4_IRQ_GPIOF           30
#define LM4_IRQ_GPIOG           31
#define LM4_IRQ_GPIOH           32
#define LM4_IRQ_UART2           33
#define LM4_IRQ_SSI1            34
#define LM4_IRQ_TIMER3A         35
#define LM4_IRQ_TIMER3B         36
#define LM4_IRQ_I2C1            37
/* 38 - 42 reserved */
#define LM4_IRQ_HIBERNATE       43
/* 44 - 45 reserved */
#define LM4_IRQ_UDMA_SOFTWARE   46
#define LM4_IRQ_UDMA_ERROR      47
#define LM4_IRQ_ADC1_SS0        48
#define LM4_IRQ_ADC1_SS1        49
#define LM4_IRQ_ADC1_SS2        50
#define LM4_IRQ_ADC1_SS3        51
/* 52 - 53 reserved */
#define LM4_IRQ_GPIOJ           54
#define LM4_IRQ_GPIOK           55
#define LM4_IRQ_GPIOL           56
#define LM4_IRQ_SSI2            57
#define LM4_IRQ_SSI3            58
#define LM4_IRQ_UART3           59
#define LM4_IRQ_UART4           60
#define LM4_IRQ_UART5           61
#define LM4_IRQ_UART6           62
#define LM4_IRQ_UART7           63
/* 64 - 67 reserved */
#define LM4_IRQ_I2C2            68
#define LM4_IRQ_I2C3            69
#define LM4_IRQ_TIMER4A         70
#define LM4_IRQ_TIMER4B         71
/* 72 - 91 reserved */
#define LM4_IRQ_TIMER5A         92
#define LM4_IRQ_TIMER5B         93
#define LM4_IRQ_TIMERW0A        94
#define LM4_IRQ_TIMERW0B        95
#define LM4_IRQ_TIMERW1A        96
#define LM4_IRQ_TIMERW1B        97
#define LM4_IRQ_TIMERW2A        98
#define LM4_IRQ_TIMERW2B        99
#define LM4_IRQ_TIMERW3A       100
#define LM4_IRQ_TIMERW3B       101
#define LM4_IRQ_TIMERW4A       102
#define LM4_IRQ_TIMERW4B       103
#define LM4_IRQ_TIMERW5A       104
#define LM4_IRQ_TIMERW5B       105
#define LM4_IRQ_SYS_EXCEPTION  106
#define LM4_IRQ_SYS_PECI       107
#define LM4_IRQ_LPC            108
#define LM4_IRQ_I2C4           109
#define LM4_IRQ_I2C5           110
#define LM4_IRQ_GPIOM          111
#define LM4_IRQ_GPION          112
/* 113 reserved */
#define LM4_IRQ_FAN            114
/* 115 reserved */
#define LM4_IRQ_GPIOP          116
#define LM4_IRQ_GPIOP1         117
#define LM4_IRQ_GPIOP2         118
#define LM4_IRQ_GPIOP3         119
#define LM4_IRQ_GPIOP4         120
#define LM4_IRQ_GPIOP5         121
#define LM4_IRQ_GPIOP6         122
#define LM4_IRQ_GPIOP7         123
#define LM4_IRQ_GPIOQ          124
#define LM4_IRQ_GPIOQ1         125
#define LM4_IRQ_GPIOQ2         126
#define LM4_IRQ_GPIOQ3         127
#define LM4_IRQ_GPIOQ4         128
#define LM4_IRQ_GPIOQ5         129
#define LM4_IRQ_GPIOQ6         130
#define LM4_IRQ_GPIOQ7         131
/* 132 - 138 reserved */

/* GPIO */
#define LM4_GPIO_PORTA_BASE         0x40004000
#define LM4_GPIO_PORTB_BASE         0x40005000
#define LM4_GPIO_PORTC_BASE         0x40006000
#define LM4_GPIO_PORTD_BASE         0x40007000
#define LM4_GPIO_PORTE_BASE         0x40024000
#define LM4_GPIO_PORTF_BASE         0x40025000
#define LM4_GPIO_PORTG_BASE         0x40026000
#define LM4_GPIO_PORTH_BASE         0x40027000
#define LM4_GPIO_PORTJ_BASE         0x4003d000
#define LM4_GPIO_PORTK_BASE         0x40061000
#define LM4_GPIO_PORTL_BASE         0x40062000
#define LM4_GPIO_PORTM_BASE         0x40063000
#define LM4_GPIO_PORTN_BASE         0x40064000
#define LM4_GPIO_PORTP_BASE         0x40065000
#define LM4_GPIO_PORTQ_BASE         0x40066000
#define LM4_GPIO_PORTA_AHB_BASE     0x40058000
#define LM4_GPIO_PORTB_AHB_BASE     0x40059000
#define LM4_GPIO_PORTC_AHB_BASE     0x4005a000
#define LM4_GPIO_PORTD_AHB_BASE     0x4005b000
#define LM4_GPIO_PORTE_AHB_BASE     0x4005c000
#define LM4_GPIO_PORTF_AHB_BASE     0x4005d000
#define LM4_GPIO_PORTG_AHB_BASE     0x4005e000
#define LM4_GPIO_PORTH_AHB_BASE     0x4005f000
#define LM4_GPIO_PORTJ_AHB_BASE     0x40060000
/* Ports for passing to LM4GPIOREG(); abstracted from base addresses above so
 * that we can switch to/from AHB. */
#define LM4_GPIO_A LM4_GPIO_PORTA_BASE
#define LM4_GPIO_B LM4_GPIO_PORTB_BASE
#define LM4_GPIO_C LM4_GPIO_PORTC_BASE
#define LM4_GPIO_D LM4_GPIO_PORTD_BASE
#define LM4_GPIO_E LM4_GPIO_PORTE_BASE
#define LM4_GPIO_F LM4_GPIO_PORTF_BASE
#define LM4_GPIO_G LM4_GPIO_PORTG_BASE
#define LM4_GPIO_H LM4_GPIO_PORTH_BASE
#define LM4_GPIO_J LM4_GPIO_PORTJ_BASE
#define LM4_GPIO_K LM4_GPIO_PORTK_BASE
#define LM4_GPIO_L LM4_GPIO_PORTL_BASE
#define LM4_GPIO_M LM4_GPIO_PORTM_BASE
#define LM4_GPIO_N LM4_GPIO_PORTN_BASE
#define LM4_GPIO_P LM4_GPIO_PORTP_BASE
#define LM4_GPIO_Q LM4_GPIO_PORTQ_BASE
#define LM4GPIOREG(port, offset)      REG32((port) + (offset))
#define LM4_GPIO_DATA(port, mask)     LM4GPIOREG(port, ((mask) << 2))
#define LM4_GPIO_DIR(port)            LM4GPIOREG(port, 0x400)
#define LM4_GPIO_IS(port)             LM4GPIOREG(port, 0x404)
#define LM4_GPIO_IBE(port)            LM4GPIOREG(port, 0x408)
#define LM4_GPIO_IEV(port)            LM4GPIOREG(port, 0x40c)
#define LM4_GPIO_IM(port)             LM4GPIOREG(port, 0x410)
#define LM4_GPIO_RIS(port)            LM4GPIOREG(port, 0x414)
#define LM4_GPIO_MIS(port)            LM4GPIOREG(port, 0x418)
#define LM4_GPIO_ICR(port)            LM4GPIOREG(port, 0x41c)
#define LM4_GPIO_AFSEL(port)          LM4GPIOREG(port, 0x420)
#define LM4_GPIO_DR2R(port)           LM4GPIOREG(port, 0x500)
#define LM4_GPIO_DR4R(port)           LM4GPIOREG(port, 0x504)
#define LM4_GPIO_DR8R(port)           LM4GPIOREG(port, 0x508)
#define LM4_GPIO_ODR(port)            LM4GPIOREG(port, 0x50c)
#define LM4_GPIO_PUR(port)            LM4GPIOREG(port, 0x510)
#define LM4_GPIO_PDR(port)            LM4GPIOREG(port, 0x514)
#define LM4_GPIO_SLR(port)            LM4GPIOREG(port, 0x518)
#define LM4_GPIO_DEN(port)            LM4GPIOREG(port, 0x51c)
#define LM4_GPIO_LOCK(port)           LM4GPIOREG(port, 0x520)
#define LM4_GPIO_CR(port)             LM4GPIOREG(port, 0x524)
#define LM4_GPIO_AMSEL(port)          LM4GPIOREG(port, 0x528)
#define LM4_GPIO_PCTL(port)           LM4GPIOREG(port, 0x52c)

/* Chip-independent aliases for port base addresses */
#define GPIO_A LM4_GPIO_A
#define GPIO_B LM4_GPIO_B
#define GPIO_C LM4_GPIO_C
#define GPIO_D LM4_GPIO_D
#define GPIO_E LM4_GPIO_E
#define GPIO_F LM4_GPIO_F
#define GPIO_G LM4_GPIO_G
#define GPIO_H LM4_GPIO_H
#define GPIO_J LM4_GPIO_J
#define GPIO_K LM4_GPIO_K
#define GPIO_L LM4_GPIO_L
#define GPIO_M LM4_GPIO_M
#define GPIO_N LM4_GPIO_N
#define GPIO_P LM4_GPIO_P
#define GPIO_Q LM4_GPIO_Q

/* Value to write to LM4_GPIO_LOCK to unlock writes */
#define LM4_GPIO_LOCK_UNLOCK          0x4c4f434b

/* I2C */
#define LM4_I2C0_BASE                 0x40020000
#define LM4_I2C1_BASE                 0x40021000
#define LM4_I2C2_BASE                 0x40022000
#define LM4_I2C3_BASE                 0x40023000
#define LM4_I2C4_BASE                 0x400c0000
#define LM4_I2C5_BASE                 0x400c1000
#define LM4_I2C_BASESEP               0x00001000
/* I2C base address by port.  Compiles to a constant in gcc if port
   and offset are constant. */
static inline int lm4_i2c_addr(int port, int offset)
{
	return offset + (port < 4 ?
			 LM4_I2C0_BASE + LM4_I2C_BASESEP * port :
			 LM4_I2C4_BASE + LM4_I2C_BASESEP * (port - 4));
}
#define LM4I2CREG(port, offset)       REG32(lm4_i2c_addr(port, offset))
#define LM4_I2C_MSA(port)             LM4I2CREG(port, 0x000)
#define LM4_I2C_MCS(port)             LM4I2CREG(port, 0x004)
#define LM4_I2C_MDR(port)             LM4I2CREG(port, 0x008)
#define LM4_I2C_MTPR(port)            LM4I2CREG(port, 0x00c)
#define LM4_I2C_MIMR(port)            LM4I2CREG(port, 0x010)
#define LM4_I2C_MRIS(port)            LM4I2CREG(port, 0x014)
#define LM4_I2C_MMIS(port)            LM4I2CREG(port, 0x018)
#define LM4_I2C_MICR(port)            LM4I2CREG(port, 0x01c)
#define LM4_I2C_MCR(port)             LM4I2CREG(port, 0x020)
#define LM4_I2C_MCLKOCNT(port)        LM4I2CREG(port, 0x024)
#define LM4_I2C_MBMON(port)           LM4I2CREG(port, 0x02c)


/* Timers */
/* Timers 0-5 are 16/32 bit */
#define LM4_TIMER0_BASE                 0x40030000
#define LM4_TIMER1_BASE                 0x40031000
#define LM4_TIMER2_BASE                 0x40032000
#define LM4_TIMER3_BASE                 0x40033000
#define LM4_TIMER4_BASE                 0x40034000
#define LM4_TIMER5_BASE                 0x40035000
/* Timers 6-11 are 32/64 bit */
#define LM4_TIMERW0_BASE                0x40036000
#define LM4_TIMERW1_BASE                0x40037000
#define LM4_TIMERW2_BASE                0x4004c000
#define LM4_TIMERW3_BASE                0x4004d000
#define LM4_TIMERW4_BASE                0x4004e000
#define LM4_TIMERW5_BASE                0x4004f000
#define LM4_TIMER_SEP                   0x00001000
static inline int lm4_timer_addr(int timer, int offset)
{
	if (timer < 8)
		return offset + LM4_TIMER0_BASE + LM4_TIMER_SEP * timer;
	else
		return offset + LM4_TIMERW2_BASE + LM4_TIMER_SEP * (timer - 8);
}
#define LM4TIMERREG(timer, offset)      REG32(lm4_timer_addr(timer, offset))
#define LM4_TIMER_CFG(tmr)              LM4TIMERREG(tmr, 0x00)
#define LM4_TIMER_TAMR(tmr)             LM4TIMERREG(tmr, 0x04)
#define LM4_TIMER_TBMR(tmr)             LM4TIMERREG(tmr, 0x08)
#define LM4_TIMER_CTL(tmr)              LM4TIMERREG(tmr, 0x0c)
#define LM4_TIMER_SYNC(tmr)             LM4TIMERREG(tmr, 0x10)
#define LM4_TIMER_IMR(tmr)              LM4TIMERREG(tmr, 0x18)
#define LM4_TIMER_RIS(tmr)              LM4TIMERREG(tmr, 0x1c)
#define LM4_TIMER_MIS(tmr)              LM4TIMERREG(tmr, 0x20)
#define LM4_TIMER_ICR(tmr)              LM4TIMERREG(tmr, 0x24)
#define LM4_TIMER_TAILR(tmr)            LM4TIMERREG(tmr, 0x28)
#define LM4_TIMER_TBILR(tmr)            LM4TIMERREG(tmr, 0x2c)
#define LM4_TIMER_TAMATCHR(tmr)         LM4TIMERREG(tmr, 0x30)
#define LM4_TIMER_TBMATCHR(tmr)         LM4TIMERREG(tmr, 0x34)
#define LM4_TIMER_TAPR(tmr)             LM4TIMERREG(tmr, 0x38)
#define LM4_TIMER_TBPR(tmr)             LM4TIMERREG(tmr, 0x3c)
#define LM4_TIMER_TAPMR(tmr)            LM4TIMERREG(tmr, 0x40)
#define LM4_TIMER_TBPMR(tmr)            LM4TIMERREG(tmr, 0x44)
#define LM4_TIMER_TAR(tmr)              LM4TIMERREG(tmr, 0x48)
#define LM4_TIMER_TBR(tmr)              LM4TIMERREG(tmr, 0x4c)
#define LM4_TIMER_TAV(tmr)              LM4TIMERREG(tmr, 0x50)
#define LM4_TIMER_TBV(tmr)              LM4TIMERREG(tmr, 0x54)
#define LM4_TIMER_RTCPD(tmr)            LM4TIMERREG(tmr, 0x58)
#define LM4_TIMER_TAPS(tmr)             LM4TIMERREG(tmr, 0x5c)
#define LM4_TIMER_TBPS(tmr)             LM4TIMERREG(tmr, 0x60)
#define LM4_TIMER_TAPV(tmr)             LM4TIMERREG(tmr, 0x64)
#define LM4_TIMER_TBPV(tmr)             LM4TIMERREG(tmr, 0x68)

#define LM4_SYSTICK_CTRL                REG32(0xe000e010)
#define LM4_SYSTICK_RELOAD              REG32(0xe000e014)
#define LM4_SYSTICK_CURRENT             REG32(0xe000e018)

/* Watchdogs */
#define LM4_WATCHDOG0_BASE              0x40000000
#define LM4_WATCHDOG1_BASE              0x40001000
static inline int lm4_watchdog_addr(int num, int offset)
{
	return offset + (num ? LM4_WATCHDOG1_BASE : LM4_WATCHDOG0_BASE);
}
#define LM4WDTREG(num, offset)		REG32(lm4_watchdog_addr(num, offset))
#define LM4_WATCHDOG_LOAD(n)            LM4WDTREG(n, 0x000)
#define LM4_WATCHDOG_VALUE(n)           LM4WDTREG(n, 0x004)
#define LM4_WATCHDOG_CTL(n)             LM4WDTREG(n, 0x008)
#define LM4_WATCHDOG_ICR(n)             LM4WDTREG(n, 0x00c)
#define LM4_WATCHDOG_RIS(n)             LM4WDTREG(n, 0x010)
#define LM4_WATCHDOG_TEST(n)            LM4WDTREG(n, 0x418)
#define LM4_WATCHDOG_LOCK(n)            LM4WDTREG(n, 0xc00)

#define LM4_TEST_MODE_ENABLED		REG32(0x400fdff0)

#endif /* __CROS_EC_REGISTERS_H */
