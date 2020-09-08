/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GPIO_CHIP_NPCX9_H
#define __CROS_EC_GPIO_CHIP_NPCX9_H

/*****************************************************************************/
/* Macro functions for MIWU mapping table */

/* MIWU0 */
/* Group A: NPCX_IRQ_MTC_WKINTAD_0 */
#define NPCX_WUI_GPIO_8_0 WUI(0, MIWU_GROUP_1, 0)
#define NPCX_WUI_GPIO_8_1 WUI(0, MIWU_GROUP_1, 1)
#define NPCX_WUI_GPIO_8_2 WUI(0, MIWU_GROUP_1, 2)
#define NPCX_WUI_GPIO_8_3 WUI(0, MIWU_GROUP_1, 3)
#define NPCX_WUI_GPIO_8_6 WUI(0, MIWU_GROUP_1, 6)
#define NPCX_WUI_GPIO_8_7 WUI(0, MIWU_GROUP_1, 7)

/* Group B: NPCX_IRQ_TWD_WKINTB_0 */
#define NPCX_WUI_GPIO_9_0 WUI(0, MIWU_GROUP_2, 0)
#define NPCX_WUI_GPIO_9_1 WUI(0, MIWU_GROUP_2, 1)
#define NPCX_WUI_GPIO_9_2 WUI(0, MIWU_GROUP_2, 2)
#define NPCX_WUI_GPIO_9_3 WUI(0, MIWU_GROUP_2, 3)
#define NPCX_WUI_GPIO_9_4 WUI(0, MIWU_GROUP_2, 4)
#define NPCX_WUI_GPIO_9_5 WUI(0, MIWU_GROUP_2, 5)

/* Group C: NPCX_IRQ_WKINTC_0 */
#define NPCX_WUI_GPIO_9_6 WUI(0, MIWU_GROUP_3, 0)
#define NPCX_WUI_GPIO_9_7 WUI(0, MIWU_GROUP_3, 1)
#define NPCX_WUI_GPIO_A_0 WUI(0, MIWU_GROUP_3, 2)
#define NPCX_WUI_GPIO_A_1 WUI(0, MIWU_GROUP_3, 3)
#define NPCX_WUI_GPIO_A_2 WUI(0, MIWU_GROUP_3, 4)
#define NPCX_WUI_GPIO_A_3 WUI(0, MIWU_GROUP_3, 5)
#define NPCX_WUI_GPIO_A_4 WUI(0, MIWU_GROUP_3, 6)
#define NPCX_WUI_GPIO_A_5 WUI(0, MIWU_GROUP_3, 7)

/* Group D: NPCX_IRQ_MTC_WKINTAD_0 */
#define NPCX_WUI_GPIO_A_6 WUI(0, MIWU_GROUP_4, 0)
#define NPCX_WUI_GPIO_A_7 WUI(0, MIWU_GROUP_4, 1)
#define NPCX_WUI_GPIO_B_0 WUI(0, MIWU_GROUP_4, 2)
#define NPCX_WUI_GPIO_B_1 WUI(0, MIWU_GROUP_4, 5)
#define NPCX_WUI_GPIO_B_2 WUI(0, MIWU_GROUP_4, 6)

/* Group E: NPCX_IRQ_WKINTEFGH_0 */
#define NPCX_WUI_GPIO_B_3 WUI(0, MIWU_GROUP_5, 0)
#define NPCX_WUI_GPIO_B_4 WUI(0, MIWU_GROUP_5, 1)
#define NPCX_WUI_GPIO_B_5 WUI(0, MIWU_GROUP_5, 2)
#define NPCX_WUI_GPIO_B_7 WUI(0, MIWU_GROUP_5, 4)

/* Group F: NPCX_IRQ_WKINTEFGH_0 */
#define NPCX_WUI_GPIO_C_0 WUI(0, MIWU_GROUP_6, 0)
#define NPCX_WUI_GPIO_C_1 WUI(0, MIWU_GROUP_6, 1)
#define NPCX_WUI_GPIO_C_2 WUI(0, MIWU_GROUP_6, 2)
#define NPCX_WUI_GPIO_C_3 WUI(0, MIWU_GROUP_6, 3)
#define NPCX_WUI_GPIO_C_4 WUI(0, MIWU_GROUP_6, 4)
#define NPCX_WUI_GPIO_C_5 WUI(0, MIWU_GROUP_6, 5)
#define NPCX_WUI_GPIO_C_6 WUI(0, MIWU_GROUP_6, 6)
#define NPCX_WUI_GPIO_C_7 WUI(0, MIWU_GROUP_6, 7)

/* Group G: NPCX_IRQ_WKINTEFGH_0 */
#define NPCX_WUI_GPIO_D_0 WUI(0, MIWU_GROUP_7, 0)
#define NPCX_WUI_GPIO_D_1 WUI(0, MIWU_GROUP_7, 1)
#define NPCX_WUI_GPIO_D_2 WUI(0, MIWU_GROUP_7, 2)
#define NPCX_WUI_GPIO_D_3 WUI(0, MIWU_GROUP_7, 3)
#define NPCX_WUI_GPIO_D_4 WUI(0, MIWU_GROUP_7, 4)
#define NPCX_WUI_GPIO_D_5 WUI(0, MIWU_GROUP_7, 5)
#define NPCX_WUI_GPIO_E_0 WUI(0, MIWU_GROUP_7, 7)

/* Group H: NPCX_IRQ_WKINTEFGH_0 */
#define NPCX_WUI_GPIO_E_1 WUI(0, MIWU_GROUP_8, 0)
#define NPCX_WUI_GPIO_E_2 WUI(0, MIWU_GROUP_8, 1)
#define NPCX_WUI_GPIO_E_3 WUI(0, MIWU_GROUP_8, 2)
#define NPCX_WUI_GPIO_E_4 WUI(0, MIWU_GROUP_8, 3)
#define NPCX_WUI_GPIO_E_5 WUI(0, MIWU_GROUP_8, 4)
#define NPCX_WUI_GPIO_F_0 WUI(0, MIWU_GROUP_8, 5)
#define NPCX_WUI_GPIO_F_3 WUI(0, MIWU_GROUP_8, 6)

/* MIWU1 */
/* Group A: NPCX_IRQ_WKINTA_1 */
#define NPCX_WUI_GPIO_0_0 WUI(1, MIWU_GROUP_1, 0)
#define NPCX_WUI_GPIO_0_1 WUI(1, MIWU_GROUP_1, 1)
#define NPCX_WUI_GPIO_0_2 WUI(1, MIWU_GROUP_1, 2)
#define NPCX_WUI_GPIO_0_3 WUI(1, MIWU_GROUP_1, 3)
#define NPCX_WUI_GPIO_0_4 WUI(1, MIWU_GROUP_1, 4)
#define NPCX_WUI_GPIO_0_5 WUI(1, MIWU_GROUP_1, 5)
#define NPCX_WUI_GPIO_0_6 WUI(1, MIWU_GROUP_1, 6)
#define NPCX_WUI_GPIO_0_7 WUI(1, MIWU_GROUP_1, 7)

/* Group B: NPCX_IRQ_WKINTB_1 */
#define NPCX_WUI_GPIO_1_0 WUI(1, MIWU_GROUP_2, 0)
#define NPCX_WUI_GPIO_1_1 WUI(1, MIWU_GROUP_2, 1)
#define NPCX_WUI_GPIO_F_4 WUI(1, MIWU_GROUP_2, 2)
#define NPCX_WUI_GPIO_1_4 WUI(1, MIWU_GROUP_2, 4)
#define NPCX_WUI_GPIO_1_5 WUI(1, MIWU_GROUP_2, 5)
#define NPCX_WUI_GPIO_1_6 WUI(1, MIWU_GROUP_2, 6)
#define NPCX_WUI_GPIO_1_7 WUI(1, MIWU_GROUP_2, 7)

/* Group C: NPCX_IRQ_KSI_WKINTC_1 */
#define NPCX_WUI_GPIO_3_1 WUI(1, MIWU_GROUP_3, 0)
#define NPCX_WUI_GPIO_3_0 WUI(1, MIWU_GROUP_3, 1)
#define NPCX_WUI_GPIO_2_7 WUI(1, MIWU_GROUP_3, 2)
#define NPCX_WUI_GPIO_2_6 WUI(1, MIWU_GROUP_3, 3)
#define NPCX_WUI_GPIO_2_5 WUI(1, MIWU_GROUP_3, 4)
#define NPCX_WUI_GPIO_2_4 WUI(1, MIWU_GROUP_3, 5)
#define NPCX_WUI_GPIO_2_3 WUI(1, MIWU_GROUP_3, 6)
#define NPCX_WUI_GPIO_2_2 WUI(1, MIWU_GROUP_3, 7)

/* Group D: NPCX_IRQ_WKINTD_1 */
#define NPCX_WUI_GPIO_2_0 WUI(1, MIWU_GROUP_4, 0)
#define NPCX_WUI_GPIO_2_1 WUI(1, MIWU_GROUP_4, 1)
#define NPCX_WUI_GPIO_F_5 WUI(1, MIWU_GROUP_4, 2)
#define NPCX_WUI_GPIO_3_3 WUI(1, MIWU_GROUP_4, 3)
#define NPCX_WUI_GPIO_3_4 WUI(1, MIWU_GROUP_4, 4)
#define NPCX_WUI_GPIO_3_6 WUI(1, MIWU_GROUP_4, 6)
#define NPCX_WUI_GPIO_3_7 WUI(1, MIWU_GROUP_4, 7)

/* Group E: NPCX_IRQ_WKINTE_1 */
#define NPCX_WUI_GPIO_4_0 WUI(1, MIWU_GROUP_5, 0)
#define NPCX_WUI_GPIO_4_1 WUI(1, MIWU_GROUP_5, 1)
#define NPCX_WUI_GPIO_4_2 WUI(1, MIWU_GROUP_5, 2)
#define NPCX_WUI_GPIO_4_3 WUI(1, MIWU_GROUP_5, 3)
#define NPCX_WUI_GPIO_4_4 WUI(1, MIWU_GROUP_5, 4)
#define NPCX_WUI_GPIO_4_5 WUI(1, MIWU_GROUP_5, 5)
#define NPCX_WUI_GPIO_4_6 WUI(1, MIWU_GROUP_5, 6)
#define NPCX_WUI_GPIO_4_7 WUI(1, MIWU_GROUP_5, 7)

/* Group F: NPCX_IRQ_WKINTF_1 */
#define NPCX_WUI_GPIO_5_0 WUI(1, MIWU_GROUP_6, 0)
#define NPCX_WUI_GPIO_5_1 WUI(1, MIWU_GROUP_6, 1)
#define NPCX_WUI_GPIO_5_2 WUI(1, MIWU_GROUP_6, 2)
#define NPCX_WUI_GPIO_5_3 WUI(1, MIWU_GROUP_6, 3)
#define NPCX_WUI_GPIO_5_4 WUI(1, MIWU_GROUP_6, 4)
#define NPCX_WUI_GPIO_5_5 WUI(1, MIWU_GROUP_6, 5)
#define NPCX_WUI_GPIO_5_6 WUI(1, MIWU_GROUP_6, 6)
#define NPCX_WUI_GPIO_5_7 WUI(1, MIWU_GROUP_6, 7)

/* Group G: NPCX_IRQ_WKINTG_1 */
#define NPCX_WUI_GPIO_6_0 WUI(1, MIWU_GROUP_7, 0)
#define NPCX_WUI_GPIO_6_1 WUI(1, MIWU_GROUP_7, 1)
#define NPCX_WUI_GPIO_6_2 WUI(1, MIWU_GROUP_7, 2)
#define NPCX_WUI_GPIO_6_3 WUI(1, MIWU_GROUP_7, 3)
#define NPCX_WUI_GPIO_6_4 WUI(1, MIWU_GROUP_7, 4)

/* Group H: NPCX_IRQ_WKINTH_1 */
#define NPCX_WUI_GPIO_7_0 WUI(1, MIWU_GROUP_8, 0)
#define NPCX_WUI_GPIO_6_7 WUI(1, MIWU_GROUP_8, 1)
#define NPCX_WUI_GPIO_7_2 WUI(1, MIWU_GROUP_8, 2)
#define NPCX_WUI_GPIO_7_3 WUI(1, MIWU_GROUP_8, 3)
#define NPCX_WUI_GPIO_7_4 WUI(1, MIWU_GROUP_8, 4)
#define NPCX_WUI_GPIO_7_5 WUI(1, MIWU_GROUP_8, 5)
#define NPCX_WUI_GPIO_7_6 WUI(1, MIWU_GROUP_8, 6)

/* MIWU2 */
/* Group F: NPCX_IRQ_WKINTFG_2 */
#define NPCX_WUI_GPIO_F_1 WUI(2, MIWU_GROUP_6, 1)
#define NPCX_WUI_GPIO_F_2 WUI(2, MIWU_GROUP_6, 2)
#define NPCX_WUI_GPIO_B_6 WUI(2, MIWU_GROUP_6, 6)

/* Others GPO without MIWU functionality */
#define NPCX_WUI_GPIO_1_2 WUI_NONE
#define NPCX_WUI_GPIO_1_3 WUI_NONE /* Software strap pin GP_SEL1_L */
#define NPCX_WUI_GPIO_3_2 WUI_NONE
#define NPCX_WUI_GPIO_3_5 WUI_NONE
#define NPCX_WUI_GPIO_6_5 WUI_NONE /* Software strap pin FLPRG_L */
#define NPCX_WUI_GPIO_6_6 WUI_NONE
#define NPCX_WUI_GPIO_7_7 WUI_NONE
#define NPCX_WUI_GPIO_8_5 WUI_NONE /* PSL_OUT/GPO85 */
#define NPCX_WUI_GPIO_D_6 WUI_NONE /* strap pin SHDF_ESPI */
#define NPCX_WUI_GPIO_D_7 WUI_NONE /* PSL_GPO/GPOD7 */

/*****************************************************************************/
/* Macro functions for Alternative mapping table */

/* I2C Module */
#define NPCX_ALT_GPIO_B_4 ALT(B, 4, NPCX_ALT(2, I2C0_0_SL)) /* SMB0SDA0 */
#define NPCX_ALT_GPIO_B_5 ALT(B, 5, NPCX_ALT(2, I2C0_0_SL)) /* SMB0SCL0 */
#define NPCX_ALT_GPIO_B_2 ALT(B, 2, NPCX_ALT(2, I2C7_0_SL)) /* SMB7SDA0 */
#define NPCX_ALT_GPIO_B_3 ALT(B, 3, NPCX_ALT(2, I2C7_0_SL)) /* SMB7SCL0 */
#define NPCX_ALT_GPIO_8_7 ALT(8, 7, NPCX_ALT(2, I2C1_0_SL)) /* SMB1SDA0 */
#define NPCX_ALT_GPIO_9_0 ALT(9, 0, NPCX_ALT(2, I2C1_0_SL)) /* SMB1SCL0 */
#define NPCX_ALT_GPIO_9_1 ALT(9, 1, NPCX_ALT(2, I2C2_0_SL)) /* SMB2SDA0 */
#define NPCX_ALT_GPIO_9_2 ALT(9, 2, NPCX_ALT(2, I2C2_0_SL)) /* SMB2SCL0 */
#define NPCX_ALT_GPIO_3_6 ALT(3, 6, NPCX_ALT(2, I2C5_0_SL)) /* SMB5SDA0 */
#define NPCX_ALT_GPIO_3_3 ALT(3, 3, NPCX_ALT(2, I2C5_0_SL)) /* SMB5SCL0 */
#define NPCX_ALT_GPIO_D_0 ALT(D, 0, NPCX_ALT(2, I2C3_0_SL)) /* SMB3SDA0 */
#define NPCX_ALT_GPIO_D_1 ALT(D, 1, NPCX_ALT(2, I2C3_0_SL)) /* SMB3SCL0 */

#define NPCX_ALT_GPIO_F_2 ALT(F, 2, NPCX_ALT(6, I2C4_1_SL)) /* SMB4SDA1 */
#define NPCX_ALT_GPIO_F_3 ALT(F, 3, NPCX_ALT(6, I2C4_1_SL)) /* SMB4SCL1 */
#define NPCX_ALT_GPIO_F_4 ALT(F, 4, NPCX_ALT(6, I2C5_1_SL)) /* SMB5SDA1 */
#define NPCX_ALT_GPIO_F_5 ALT(F, 5, NPCX_ALT(6, I2C5_1_SL)) /* SMB5SCL1 */
#define NPCX_ALT_GPIO_E_3 ALT(E, 3, NPCX_ALT(6, I2C6_1_SL)) /* SMB6SDA1 */
#define NPCX_ALT_GPIO_E_4 ALT(E, 4, NPCX_ALT(6, I2C6_1_SL)) /* SMB6SCL1 */
/* Pin-Mux for PWM1/SMB6_0 */
#if NPCX9_PWM1_SEL
#define NPCX_ALT_GPIO_C_1 /* No I2CSDA since GPIOC2 used as PWM1 */
#define NPCX_ALT_GPIO_C_2 ALT(C, 2, NPCX_ALT(4, PWM1_SL))   /* PWM1 */
#else
#define NPCX_ALT_GPIO_C_1 ALT(C, 1, NPCX_ALT(2, I2C6_0_SL)) /* SMB6SDA0 */
#define NPCX_ALT_GPIO_C_2 ALT(C, 2, NPCX_ALT(2, I2C6_0_SL)) /* SMB6SCL0 */
#endif

/* ADC Module */
#define NPCX_ALT_GPIO_4_5 ALT(4, 5, NPCX_ALT(6, ADC0_SL)) /* ADC0  */
#define NPCX_ALT_GPIO_4_4 ALT(4, 4, NPCX_ALT(6, ADC1_SL)) /* ADC1  */
#define NPCX_ALT_GPIO_4_3 ALT(4, 3, NPCX_ALT(6, ADC2_SL)) /* ADC2  */
#define NPCX_ALT_GPIO_4_2 ALT(4, 2, NPCX_ALT(6, ADC3_SL)) /* ADC3  */
#define NPCX_ALT_GPIO_4_1 ALT(4, 1, NPCX_ALT(6, ADC4_SL)) /* ADC4  */
#ifdef CONFIG_PS2
#define NPCX_ALT_GPIO_3_7 ALT(3, 7, NPCX_ALT(3, PS2_2_SL)) /* PS2_CLK2 */
#define NPCX_ALT_GPIO_3_4 ALT(3, 4, NPCX_ALT(3, PS2_2_SL)) /* PS2_DATA2 */
#else
#define NPCX_ALT_GPIO_3_7 ALT(3, 7, NPCX_ALT(F, ADC5_SL)) /* ADC5  */
#define NPCX_ALT_GPIO_3_4 ALT(3, 4, NPCX_ALT(F, ADC6_SL)) /* ADC6  */
#endif
#define NPCX_ALT_GPIO_F_1 ALT(F, 1, NPCX_ALT(F, ADC8_SL))  /* ADC8  */
#define NPCX_ALT_GPIO_E_1 ALT(E, 1, NPCX_ALT(F, ADC7_SL))  /* ADC7  */
#define NPCX_ALT_GPIO_F_0 ALT(F, 0, NPCX_ALT(F, ADC9_SL))  /* ADC9  */
#define NPCX_ALT_GPIO_E_0 ALT(E, 0, NPCX_ALT(F, ADC10_SL)) /* AD10  */
#define NPCX_ALT_GPIO_C_7 ALT(C, 7, NPCX_ALT(F, ADC11_SL)) /* AD11  */

/* PS/2 Module */
#define NPCX_ALT_GPIO_6_7 ALT(6, 7, NPCX_ALT(3, PS2_0_SL))  /* PS2_CLK0 */
#define NPCX_ALT_GPIO_7_0 ALT(7, 0, NPCX_ALT(3, PS2_0_SL))  /* PS2_DATA0 */
#define NPCX_ALT_GPIO_6_2 ALT(6, 2, NPCX_ALT(3, PS2_1_SL))  /* PS2_CLK1 */
#define NPCX_ALT_GPIO_6_3 ALT(6, 3, NPCX_ALT(3, PS2_1_SL))  /* PS2_DATA1 */
#define NPCX_ALT_GPIO_A_7 ALT(A, 7, NPCX_ALT(C, PS2_3_SL2)) /* PS2_DAT3 */

/* UART Module */
#define NPCX_ALT_GPIO_6_4 ALT(6, 4, NPCX_ALT(J, CR_SIN1_SL2)) /* CR_SIN1_SL2 */
#define NPCX_ALT_GPIO_6_5 ALT(6, 5, NPCX_ALT(J, CR_SOUT1_SL2))/* CR_SOUT1_SL2 */
#define NPCX_ALT_GPIO_7_5 ALT(7, 5, NPCX_ALT(J, CR_SIN2_SL))  /* CR_SIN2_SL */
#define NPCX_ALT_GPIO_8_6 ALT(8, 6, NPCX_ALT(J, CR_SOUT2_SL)) /* CR_SOUT2_SL */
#define NPCX_ALT_GPIO_D_4 ALT(D, 4, NPCX_ALT(J, CR_SIN3_SL))  /* CR_SIN3_SL */
#define NPCX_ALT_GPIO_D_6 ALT(D, 6, NPCX_ALT(J, CR_SOUT3_SL)) /* CR_SOUT3_SL */

/* PWM Module */
#define NPCX_ALT_GPIO_C_3 ALT(C, 3, NPCX_ALT(4, PWM0_SL)) /* PWM0 */
#define NPCX_ALT_GPIO_C_4 ALT(C, 4, NPCX_ALT(4, PWM2_SL)) /* PWM2 */
#define NPCX_ALT_GPIO_8_0 ALT(8, 0, NPCX_ALT(4, PWM3_SL)) /* PWM3 */
#define NPCX_ALT_GPIO_B_6 ALT(B, 6, NPCX_ALT(4, PWM4_SL)) /* PWM4 */
#define NPCX_ALT_GPIO_B_7 ALT(B, 7, NPCX_ALT(4, PWM5_SL)) /* PWM5 */
#define NPCX_ALT_GPIO_C_0 ALT(C, 0, NPCX_ALT(4, PWM6_SL)) /* PWM6 */
#define NPCX_ALT_GPIO_6_0 ALT(6, 0, NPCX_ALT(4, PWM7_SL)) /* PWM7 */

/* MFT Module */
#define NPCX_ALT_GPIO_4_0 ALT(4, 0, NPCX_ALT(3, TA1_SL1)) /* TA1_SEL1 */
#define NPCX_ALT_GPIO_7_3 ALT(7, 3, NPCX_ALT(3, TA2_SL1)) /* TA2_SEL1 */
#define NPCX_ALT_GPIO_9_3 ALT(9, 3, NPCX_ALT(C, TA1_SL2)) /* TA1_SEL2 */
#ifdef CONFIG_PS2
#define NPCX_ALT_GPIO_A_6 ALT(A, 6, NPCX_ALT(C, PS2_3_SL2)) /* PS2_CLK3 */
#else
#define NPCX_ALT_GPIO_A_6 ALT(A, 6, NPCX_ALT(C, TA2_SL2)) /* TA2_SEL2 */
#endif

/* Keyboard Scan Module */
#define NPCX_ALT_GPIO_3_1 ALT(3, 1, NPCX_ALT_INV(7, NO_KSI0_SL)) /* KSI0 */
#define NPCX_ALT_GPIO_3_0 ALT(3, 0, NPCX_ALT_INV(7, NO_KSI1_SL)) /* KSI1 */
#define NPCX_ALT_GPIO_2_7 ALT(2, 7, NPCX_ALT_INV(7, NO_KSI2_SL)) /* KSI2 */
#define NPCX_ALT_GPIO_2_6 ALT(2, 6, NPCX_ALT_INV(7, NO_KSI3_SL)) /* KSI3 */
#define NPCX_ALT_GPIO_2_5 ALT(2, 5, NPCX_ALT_INV(7, NO_KSI4_SL)) /* KSI4 */
#define NPCX_ALT_GPIO_2_4 ALT(2, 4, NPCX_ALT_INV(7, NO_KSI5_SL)) /* KSI5 */
#define NPCX_ALT_GPIO_2_3 ALT(2, 3, NPCX_ALT_INV(7, NO_KSI6_SL)) /* KSI6 */
#define NPCX_ALT_GPIO_2_2 ALT(2, 2, NPCX_ALT_INV(7, NO_KSI7_SL)) /* KSI7 */
#define NPCX_ALT_GPIO_2_1 ALT(2, 1, NPCX_ALT_INV(8, NO_KSO00_SL)) /* KSO00 */
#define NPCX_ALT_GPIO_2_0 ALT(2, 0, NPCX_ALT_INV(8, NO_KSO01_SL)) /* KSO01 */
#define NPCX_ALT_GPIO_1_7 ALT(1, 7, NPCX_ALT_INV(8, NO_KSO02_SL)) /* KSO02 */
#define NPCX_ALT_GPIO_1_6 ALT(1, 6, NPCX_ALT_INV(8, NO_KSO03_SL)) /* KSO03 */
#define NPCX_ALT_GPIO_1_5 ALT(1, 5, NPCX_ALT_INV(8, NO_KSO04_SL)) /* KSO04 */
#define NPCX_ALT_GPIO_1_4 ALT(1, 4, NPCX_ALT_INV(8, NO_KSO05_SL)) /* KSO05 */
#define NPCX_ALT_GPIO_1_3 ALT(1, 3, NPCX_ALT_INV(8, NO_KSO06_SL)) /* KSO06 */
#define NPCX_ALT_GPIO_1_2 ALT(1, 2, NPCX_ALT_INV(8, NO_KSO07_SL)) /* KSO07 */
/* KSO08 & CR_SOUT */
#define NPCX_ALT_GPIO_1_1 ALT(1, 1, NPCX_ALT_INV(9, NO_KSO08_SL))
 /* KSO09 & CR_SIN */
#define NPCX_ALT_GPIO_1_0 ALT(1, 0, NPCX_ALT_INV(9, NO_KSO09_SL))
#define NPCX_ALT_GPIO_0_7 ALT(0, 7, NPCX_ALT_INV(9, NO_KSO10_SL)) /* KSO10 */
#define NPCX_ALT_GPIO_0_6 ALT(0, 6, NPCX_ALT_INV(9, NO_KSO11_SL)) /* KSO11 */
#define NPCX_ALT_GPIO_0_5 ALT(0, 5, NPCX_ALT_INV(9, NO_KSO12_SL)) /* KSO12 */
#define NPCX_ALT_GPIO_0_4 ALT(0, 4, NPCX_ALT_INV(9, NO_KSO13_SL)) /* KSO13 */
#define NPCX_ALT_GPIO_8_2 ALT(8, 2, NPCX_ALT_INV(9, NO_KSO14_SL)) /* KSO14 */
#define NPCX_ALT_GPIO_8_3 ALT(8, 3, NPCX_ALT_INV(9, NO_KSO15_SL)) /* KSO15 */
#define NPCX_ALT_GPIO_0_3 ALT(0, 3, NPCX_ALT_INV(A, NO_KSO16_SL)) /* KSO16 */
#define NPCX_ALT_GPIO_B_1 ALT(B, 1, NPCX_ALT_INV(A, NO_KSO17_SL)) /* KSO17 */

/* PSL module */
#define NPCX_ALT_GPIO_D_2 ALT(D, 2, NPCX_ALT_INV(D, NPSL_IN1_SL)) /* PSL_IN1 */
#define NPCX_ALT_GPIO_0_0 ALT(0, 0, NPCX_ALT_INV(D, NPSL_IN2_SL)) /* PSL_IN2 */
#define NPCX_ALT_GPIO_0_1 ALT(0, 1, NPCX_ALT(D, PSL_IN3_SL))      /* PSL_IN3 */
#define NPCX_ALT_GPIO_0_2 ALT(0, 2, NPCX_ALT(D, PSL_IN4_SL))      /* PSL_IN4 */
#define NPCX_ALT_GPIO_D_7 ALT(D, 7, NPCX_ALT(G, PSL_GPO_SL))      /* PSL_GPO */

/* SPI Module */
#define NPCX_ALT_GPIO_9_5 ALT(9, 5, NPCX_ALT(0, SPIP_SL)) /* SPIP_MISO */
#define NPCX_ALT_GPIO_A_3 ALT(A, 3, NPCX_ALT(0, SPIP_SL)) /* SPIP_MOSI */
#define NPCX_ALT_GPIO_A_1 ALT(A, 1, NPCX_ALT(0, SPIP_SL)) /* SPIP_SCLK */

#define NPCX_ALT_TABLE { \
	NPCX_ALT_GPIO_0_0 /* PSL_IN2 */               \
	NPCX_ALT_GPIO_0_1 /* PSL_IN3 */               \
	NPCX_ALT_GPIO_0_2 /* PSL_IN4 */               \
	NPCX_ALT_GPIO_0_3 /* KSO16 */                 \
	NPCX_ALT_GPIO_0_4 /* KSO13 */                 \
	NPCX_ALT_GPIO_0_5 /* KSO12 */                 \
	NPCX_ALT_GPIO_0_6 /* KSO11 */                 \
	NPCX_ALT_GPIO_0_7 /* KSO10 */                 \
	NPCX_ALT_GPIO_1_0 /* KSO09 & CR_SIN */        \
	NPCX_ALT_GPIO_1_1 /* KSO08 & CR_SOUT */       \
	NPCX_ALT_GPIO_1_2 /* KSO07 */                 \
	NPCX_ALT_GPIO_1_3 /* KSO06 */                 \
	NPCX_ALT_GPIO_1_4 /* KSO05 */                 \
	NPCX_ALT_GPIO_1_5 /* KSO04 */                 \
	NPCX_ALT_GPIO_1_6 /* KSO03 */                 \
	NPCX_ALT_GPIO_1_7 /* KSO02 */                 \
	NPCX_ALT_GPIO_2_0 /* KSO01 */                 \
	NPCX_ALT_GPIO_2_1 /* KSO00 */                 \
	NPCX_ALT_GPIO_2_2 /* KSI7 */                  \
	NPCX_ALT_GPIO_2_3 /* KSI6 */                  \
	NPCX_ALT_GPIO_2_4 /* KSI5 */                  \
	NPCX_ALT_GPIO_2_5 /* KSI4 */                  \
	NPCX_ALT_GPIO_2_6 /* KSI3 */                  \
	NPCX_ALT_GPIO_2_7 /* KSI2 */                  \
	NPCX_ALT_GPIO_3_0 /* KSI1 */                  \
	NPCX_ALT_GPIO_3_1 /* KSI0 */                  \
	NPCX_ALT_GPIO_3_3 /* SMB5SCL0 */              \
	NPCX_ALT_GPIO_3_4 /* ADC6/PS2_DAT2 */         \
	NPCX_ALT_GPIO_3_6 /* SMB5SDA0 */              \
	NPCX_ALT_GPIO_3_7 /* ADC5/PS2_CLK2 */         \
	NPCX_ALT_GPIO_4_0 /* TA1_SEL1 */              \
	NPCX_ALT_GPIO_4_1 /* ADC4  */                 \
	NPCX_ALT_GPIO_4_2 /* ADC3  */                 \
	NPCX_ALT_GPIO_4_3 /* ADC2  */                 \
	NPCX_ALT_GPIO_4_4 /* ADC1  */                 \
	NPCX_ALT_GPIO_4_5 /* ADC0  */                 \
	NPCX_ALT_GPIO_6_0 /* PWM7 */                  \
	NPCX_ALT_GPIO_6_2 /* PS2_CLK1 */              \
	NPCX_ALT_GPIO_6_3 /* PS2_DAT1 */              \
	NPCX_ALT_GPIO_6_4 /* CR_SIN1_SL2 */           \
	NPCX_ALT_GPIO_6_5 /* CR_SOUT1_SL2 */          \
	NPCX_ALT_GPIO_6_7 /* PS2_CLK0 */              \
	NPCX_ALT_GPIO_7_0 /* PS2_DAT0 */              \
	NPCX_ALT_GPIO_7_3 /* TA2_SEL1 */              \
	NPCX_ALT_GPIO_7_5 /* CR_SIN2_SL */            \
	NPCX_ALT_GPIO_8_0 /* PWM3 */                  \
	NPCX_ALT_GPIO_8_2 /* KSO14 */                 \
	NPCX_ALT_GPIO_8_3 /* KSO15 */                 \
	NPCX_ALT_GPIO_8_6 /* CR_SOUT2_SL */           \
	NPCX_ALT_GPIO_8_7 /* SMB1SDA0 */              \
	NPCX_ALT_GPIO_9_0 /* SMB1SCL0 */              \
	NPCX_ALT_GPIO_9_1 /* SMB2SDA0 */              \
	NPCX_ALT_GPIO_9_2 /* SMB2SCL0 */              \
	NPCX_ALT_GPIO_9_3 /* TA1_SEL2 */              \
	NPCX_ALT_GPIO_9_5 /* SPIP_MISO */             \
	NPCX_ALT_GPIO_A_1 /* SPIP_SCLK */             \
	NPCX_ALT_GPIO_A_3 /* SPIP_MOSI */             \
	NPCX_ALT_GPIO_A_6 /* TA2_SEL2/PS2_CLK3 */     \
	NPCX_ALT_GPIO_A_7 /* I2S_SCLK/PS2_DAT3 */     \
	NPCX_ALT_GPIO_B_1 /* KSO17 */                 \
	NPCX_ALT_GPIO_B_2 /* SMB7SDA0 */              \
	NPCX_ALT_GPIO_B_3 /* SMB7SCL0 */              \
	NPCX_ALT_GPIO_B_4 /* SMB0SDA0 */              \
	NPCX_ALT_GPIO_B_5 /* SMB0SCL0 */              \
	NPCX_ALT_GPIO_B_6 /* PWM4 */                  \
	NPCX_ALT_GPIO_B_7 /* PWM5 */                  \
	NPCX_ALT_GPIO_C_0 /* PWM6 */                  \
	NPCX_ALT_GPIO_C_1 /* SMB6SDA0 */              \
	NPCX_ALT_GPIO_C_2 /* SMB6SCL0 & PWM1 */       \
	NPCX_ALT_GPIO_C_3 /* PWM0 */                  \
	NPCX_ALT_GPIO_C_4 /* PWM2 */                  \
	NPCX_ALT_GPIO_C_7 /* ADC11 */                 \
	NPCX_ALT_GPIO_D_0 /* SMB3SDA0 */              \
	NPCX_ALT_GPIO_D_1 /* SMB3SCL0 */              \
	NPCX_ALT_GPIO_D_2 /* PSL_IN1 */               \
	NPCX_ALT_GPIO_D_4 /* CR_SIN3_SL */            \
	NPCX_ALT_GPIO_D_6 /* CR_SOUT3_SL */           \
	NPCX_ALT_GPIO_D_7 /* PSL_GPO */               \
	NPCX_ALT_GPIO_E_0 /* ADC10 */                 \
	NPCX_ALT_GPIO_E_1 /* ADC7  */                 \
	NPCX_ALT_GPIO_E_3 /* SMB6SDA1 */              \
	NPCX_ALT_GPIO_E_4 /* SMB6SCL1 */              \
	NPCX_ALT_GPIO_F_0 /* ADC9  */                 \
	NPCX_ALT_GPIO_F_1 /* ADC8  */                 \
	NPCX_ALT_GPIO_F_2 /* SMB4SDA1 */              \
	NPCX_ALT_GPIO_F_3 /* SMB4SCL1 */              \
	NPCX_ALT_GPIO_F_4 /* SMB5SDA1 */              \
	NPCX_ALT_GPIO_F_5 /* SMB5SCL1 */              \
}

/*****************************************************************************/
/* Macro functions for Low-Voltage mapping table */

/* Low-Voltage GPIO Control 0 */
#define NPCX_LVOL_CTRL_0_0  NPCX_GPIO(B, 5)
#define NPCX_LVOL_CTRL_0_1  NPCX_GPIO(B, 4)
#define NPCX_LVOL_CTRL_0_2  NPCX_GPIO(B, 3)
#define NPCX_LVOL_CTRL_0_3  NPCX_GPIO(B, 2)
#define NPCX_LVOL_CTRL_0_4  NPCX_GPIO(9, 0)
#define NPCX_LVOL_CTRL_0_5  NPCX_GPIO(8, 7)
#define NPCX_LVOL_CTRL_0_6  NPCX_GPIO(0, 0)
#define NPCX_LVOL_CTRL_0_7  NPCX_GPIO(3, 3)

/* Low-Voltage GPIO Control 1 */
#define NPCX_LVOL_CTRL_1_0  NPCX_GPIO(9, 2)
#define NPCX_LVOL_CTRL_1_1  NPCX_GPIO(9, 1)
#define NPCX_LVOL_CTRL_1_2  NPCX_GPIO(D, 1)
#define NPCX_LVOL_CTRL_1_3  NPCX_GPIO(D, 0)
#define NPCX_LVOL_CTRL_1_4  NPCX_GPIO(3, 6)
#define NPCX_LVOL_CTRL_1_5  NPCX_GPIO(6, 4)
#define NPCX_LVOL_CTRL_1_6  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_1_7  NPCX_GPIO_NONE

/* Low-Voltage GPIO Control 2 */
#define NPCX_LVOL_CTRL_2_0  NPCX_GPIO(7, 4)
#define NPCX_LVOL_CTRL_2_1  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_2_2  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_2_3  NPCX_GPIO(7, 3)
#define NPCX_LVOL_CTRL_2_4  NPCX_GPIO(C, 1)
#define NPCX_LVOL_CTRL_2_5  NPCX_GPIO(C, 7)
#define NPCX_LVOL_CTRL_2_6  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_2_7  NPCX_GPIO(3, 4)

/* Low-Voltage GPIO Control 3 */
#define NPCX_LVOL_CTRL_3_0  NPCX_GPIO(C, 6)
#define NPCX_LVOL_CTRL_3_1  NPCX_GPIO(3, 7)
#define NPCX_LVOL_CTRL_3_2  NPCX_GPIO(4, 0)
#define NPCX_LVOL_CTRL_3_3  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_3_4  NPCX_GPIO(8, 2)
#define NPCX_LVOL_CTRL_3_5  NPCX_GPIO(7, 5)
#define NPCX_LVOL_CTRL_3_6  NPCX_GPIO(8, 0)
#define NPCX_LVOL_CTRL_3_7  NPCX_GPIO(C, 5)

/* Low-Voltage GPIO Control 4 */
#define NPCX_LVOL_CTRL_4_0  NPCX_GPIO(8, 6)
#define NPCX_LVOL_CTRL_4_1  NPCX_GPIO(C, 2)
#define NPCX_LVOL_CTRL_4_2  NPCX_GPIO(F, 3)
#define NPCX_LVOL_CTRL_4_3  NPCX_GPIO(F, 2)
#define NPCX_LVOL_CTRL_4_4  NPCX_GPIO(F, 5)
#define NPCX_LVOL_CTRL_4_5  NPCX_GPIO(F, 4)
#define NPCX_LVOL_CTRL_4_6  NPCX_GPIO(E, 4)
#define NPCX_LVOL_CTRL_4_7  NPCX_GPIO(E, 3)

/* Low-Voltage GPIO Control 5 */
#define NPCX_LVOL_CTRL_5_0  NPCX_GPIO(7, 2)
#define NPCX_LVOL_CTRL_5_1  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_5_2  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_5_3  NPCX_GPIO(5, 0)
#define NPCX_LVOL_CTRL_5_4  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_5_5  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_5_6  NPCX_GPIO_NONE
#define NPCX_LVOL_CTRL_5_7  NPCX_GPIO_NONE

/* 6 Low-Voltage Control Groups on npcx7 */
#define NPCX_LVOL_TABLE { { NPCX_LVOL_CTRL_ITEMS(0), }, \
			  { NPCX_LVOL_CTRL_ITEMS(1), }, \
			  { NPCX_LVOL_CTRL_ITEMS(2), }, \
			  { NPCX_LVOL_CTRL_ITEMS(3), }, \
			  { NPCX_LVOL_CTRL_ITEMS(4), }, \
			  { NPCX_LVOL_CTRL_ITEMS(5), }, }

#endif /* __CROS_EC_GPIO_CHIP_NPCX9_H */
