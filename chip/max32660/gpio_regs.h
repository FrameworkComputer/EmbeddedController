/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 Registers, Bit Masks and Bit Positions for the GPIO Peripheral
 * Module */

#ifndef _GPIO_REGS_H_
#define _GPIO_REGS_H_

/* **** Includes **** */
#include <stdint.h>

/*
    If types are not defined elsewhere (CMSIS) define them here
*/
#ifndef __IO
#define __IO volatile
#endif
#ifndef __I
#define __I volatile const
#endif
#ifndef __O
#define __O volatile
#endif
#ifndef __R
#define __R volatile const
#endif

/* **** Definitions **** */

/**
 * gpio
 * Individual I/O for each GPIO
 */

/**
 * gpio_registers
 * Structure type to access the GPIO Registers.
 */
typedef struct {
	__IO uint32_t en; /**< <tt>\b 0x00:<\tt> GPIO EN Register */
	__IO uint32_t en_set; /**< <tt>\b 0x04:<\tt> GPIO EN_SET Register */
	__IO uint32_t en_clr; /**< <tt>\b 0x08:<\tt> GPIO EN_CLR Register */
	__IO uint32_t out_en; /**< <tt>\b 0x0C:<\tt> GPIO OUT_EN Register */
	__IO uint32_t out_en_set; /**< <tt>\b 0x10:<\tt> GPIO OUT_EN_SET
				     Register */
	__IO uint32_t out_en_clr; /**< <tt>\b 0x14:<\tt> GPIO OUT_EN_CLR
				     Register */
	__IO uint32_t out; /**< <tt>\b 0x18:<\tt> GPIO OUT Register */
	__O uint32_t out_set; /**< <tt>\b 0x1C:<\tt> GPIO OUT_SET Register */
	__O uint32_t out_clr; /**< <tt>\b 0x20:<\tt> GPIO OUT_CLR Register */
	__I uint32_t in; /**< <tt>\b 0x24:<\tt> GPIO IN Register */
	__IO uint32_t int_mod; /**< <tt>\b 0x28:<\tt> GPIO INT_MOD Register */
	__IO uint32_t int_pol; /**< <tt>\b 0x2C:<\tt> GPIO INT_POL Register */
	__R uint32_t rsv_0x30;
	__IO uint32_t int_en; /**< <tt>\b 0x34:<\tt> GPIO INT_EN Register */
	__IO uint32_t int_en_set; /**< <tt>\b 0x38:<\tt> GPIO INT_EN_SET
				     Register */
	__IO uint32_t int_en_clr; /**< <tt>\b 0x3C:<\tt> GPIO INT_EN_CLR
				     Register */
	__I uint32_t int_stat; /**< <tt>\b 0x40:<\tt> GPIO INT_STAT Register */
	__R uint32_t rsv_0x44;
	__IO uint32_t int_clr; /**< <tt>\b 0x48:<\tt> GPIO INT_CLR Register */
	__IO uint32_t wake_en; /**< <tt>\b 0x4C:<\tt> GPIO WAKE_EN Register */
	__IO uint32_t wake_en_set; /**< <tt>\b 0x50:<\tt> GPIO WAKE_EN_SET
				      Register */
	__IO uint32_t wake_en_clr; /**< <tt>\b 0x54:<\tt> GPIO WAKE_EN_CLR
				      Register */
	__R uint32_t rsv_0x58;
	__IO uint32_t int_dual_edge; /**< <tt>\b 0x5C:<\tt> GPIO INT_DUAL_EDGE
					Register */
	__IO uint32_t pad_cfg1; /**< <tt>\b 0x60:<\tt> GPIO PAD_CFG1 Register */
	__IO uint32_t pad_cfg2; /**< <tt>\b 0x64:<\tt> GPIO PAD_CFG2 Register */
	__IO uint32_t en1; /**< <tt>\b 0x68:<\tt> GPIO EN1 Register */
	__IO uint32_t en1_set; /**< <tt>\b 0x6C:<\tt> GPIO EN1_SET Register */
	__IO uint32_t en1_clr; /**< <tt>\b 0x70:<\tt> GPIO EN1_CLR Register */
	__IO uint32_t en2; /**< <tt>\b 0x74:<\tt> GPIO EN2 Register */
	__IO uint32_t en2_set; /**< <tt>\b 0x78:<\tt> GPIO EN2_SET Register */
	__IO uint32_t en2_clr; /**< <tt>\b 0x7C:<\tt> GPIO EN2_CLR Register */
	__R uint32_t rsv_0x80_0xa7[10];
	__IO uint32_t is; /**< <tt>\b 0xA8:<\tt> GPIO IS Register */
	__IO uint32_t sr; /**< <tt>\b 0xAC:<\tt> GPIO SR Register */
	__IO uint32_t ds; /**< <tt>\b 0xB0:<\tt> GPIO DS Register */
	__IO uint32_t ds1; /**< <tt>\b 0xB4:<\tt> GPIO DS1 Register */
	__IO uint32_t ps; /**< <tt>\b 0xB8:<\tt> GPIO PS Register */
	__R uint32_t rsv_0xbc;
	__IO uint32_t vssel; /**< <tt>\b 0xC0:<\tt> GPIO VSSEL Register */
} mxc_gpio_regs_t;

#define PIN_0 ((uint32_t)(1UL << 0)) /**< Pin 0 Define */
#define PIN_1 ((uint32_t)(1UL << 1)) /**< Pin 1 Define */
#define PIN_2 ((uint32_t)(1UL << 2)) /**< Pin 2 Define */
#define PIN_3 ((uint32_t)(1UL << 3)) /**< Pin 3 Define */
#define PIN_4 ((uint32_t)(1UL << 4)) /**< Pin 4 Define */
#define PIN_5 ((uint32_t)(1UL << 5)) /**< Pin 5 Define */
#define PIN_6 ((uint32_t)(1UL << 6)) /**< Pin 6 Define */
#define PIN_7 ((uint32_t)(1UL << 7)) /**< Pin 7 Define */
#define PIN_8 ((uint32_t)(1UL << 8)) /**< Pin 8 Define */
#define PIN_9 ((uint32_t)(1UL << 9)) /**< Pin 9 Define */
#define PIN_10 ((uint32_t)(1UL << 10)) /**< Pin 10 Define */
#define PIN_11 ((uint32_t)(1UL << 11)) /**< Pin 11 Define */
#define PIN_12 ((uint32_t)(1UL << 12)) /**< Pin 12 Define */
#define PIN_13 ((uint32_t)(1UL << 13)) /**< Pin 13 Define */
#define PIN_14 ((uint32_t)(1UL << 14)) /**< Pin 14 Define */
#define PIN_15 ((uint32_t)(1UL << 15)) /**< Pin 15 Define */
#define PIN_16 ((uint32_t)(1UL << 16)) /**< Pin 16 Define */
#define PIN_17 ((uint32_t)(1UL << 17)) /**< Pin 17 Define */
#define PIN_18 ((uint32_t)(1UL << 18)) /**< Pin 18 Define */
#define PIN_19 ((uint32_t)(1UL << 19)) /**< Pin 19 Define */
#define PIN_20 ((uint32_t)(1UL << 20)) /**< Pin 20 Define */
#define PIN_21 ((uint32_t)(1UL << 21)) /**< Pin 21 Define */
#define PIN_22 ((uint32_t)(1UL << 22)) /**< Pin 22 Define */
#define PIN_23 ((uint32_t)(1UL << 23)) /**< Pin 23 Define */
#define PIN_24 ((uint32_t)(1UL << 24)) /**< Pin 24 Define */
#define PIN_25 ((uint32_t)(1UL << 25)) /**< Pin 25 Define */
#define PIN_26 ((uint32_t)(1UL << 26)) /**< Pin 26 Define */
#define PIN_27 ((uint32_t)(1UL << 27)) /**< Pin 27 Define */
#define PIN_28 ((uint32_t)(1UL << 28)) /**< Pin 28 Define */
#define PIN_29 ((uint32_t)(1UL << 29)) /**< Pin 29 Define */
#define PIN_30 ((uint32_t)(1UL << 30)) /**< Pin 30 Define */
#define PIN_31 ((uint32_t)(1UL << 31)) /**< Pin 31 Define */

/**
 * Enumeration type for the GPIO Function Type
 */
typedef enum {
	GPIO_FUNC_IN, /**< GPIO Input */
	GPIO_FUNC_OUT, /**< GPIO Output */
	GPIO_FUNC_ALT1, /**< Alternate Function Selection */
	GPIO_FUNC_ALT2, /**< Alternate Function Selection */
	GPIO_FUNC_ALT3, /**< Alternate Function Selection */
	GPIO_FUNC_ALT4, /**< Alternate Function Selection */
} gpio_func_t;

/**
 * Enumeration type for the type of GPIO pad on a given pin.
 */
typedef enum {
	GPIO_PAD_NONE, /**< No pull-up or pull-down */
	GPIO_PAD_PULL_UP, /**< Set pad to weak pull-up */
	GPIO_PAD_PULL_DOWN, /**< Set pad to weak pull-down */
} gpio_pad_t;

/**
 * Structure type for configuring a GPIO port.
 */
typedef struct {
	uint32_t port; /**< Index of GPIO port */
	uint32_t mask; /**< Pin mask (multiple pins may be set) */
	gpio_func_t func; /**< Function type */
	gpio_pad_t pad; /**< Pad type */
} gpio_cfg_t;

typedef enum { GPIO_INTERRUPT_LEVEL, GPIO_INTERRUPT_EDGE } gpio_int_mode_t;

typedef enum {
	GPIO_INTERRUPT_FALLING = 0, /**< Interrupt triggers on falling edge */
	GPIO_INTERRUPT_HIGH = GPIO_INTERRUPT_FALLING, /**< Interrupt triggers
							 when level is high */
	GPIO_INTERRUPT_RISING, /**< Interrupt triggers on rising edge */
	GPIO_INTERRUPT_LOW = GPIO_INTERRUPT_RISING, /**< Interrupt triggers when
						       level is low */
	GPIO_INTERRUPT_BOTH /**< Interrupt triggers on either edge */
} gpio_int_pol_t;

/* Register offsets for module GPIO */
#define MXC_R_GPIO_EN                                                     \
	((uint32_t)0x00000000UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x000 */
#define MXC_R_GPIO_EN_SET                                                 \
	((uint32_t)0x00000004UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x004 */
#define MXC_R_GPIO_EN_CLR                                                 \
	((uint32_t)0x00000008UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x008 */
#define MXC_R_GPIO_OUT_EN                                                 \
	((uint32_t)0x0000000CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x00C */
#define MXC_R_GPIO_OUT_EN_SET                                             \
	((uint32_t)0x00000010UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x010 */
#define MXC_R_GPIO_OUT_EN_CLR                                             \
	((uint32_t)0x00000014UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x014 */
#define MXC_R_GPIO_OUT                                                    \
	((uint32_t)0x00000018UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x018 */
#define MXC_R_GPIO_OUT_SET                                                \
	((uint32_t)0x0000001CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x01C */
#define MXC_R_GPIO_OUT_CLR                                                \
	((uint32_t)0x00000020UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x020 */
#define MXC_R_GPIO_IN                                                     \
	((uint32_t)0x00000024UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x024 */
#define MXC_R_GPIO_INT_MOD                                                \
	((uint32_t)0x00000028UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x028 */
#define MXC_R_GPIO_INT_POL                                                \
	((uint32_t)0x0000002CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x02C */
#define MXC_R_GPIO_INT_EN                                                 \
	((uint32_t)0x00000034UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x034 */
#define MXC_R_GPIO_INT_EN_SET                                             \
	((uint32_t)0x00000038UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x038 */
#define MXC_R_GPIO_INT_EN_CLR                                             \
	((uint32_t)0x0000003CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x03C */
#define MXC_R_GPIO_INT_STAT                                               \
	((uint32_t)0x00000040UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x040 */
#define MXC_R_GPIO_INT_CLR                                                \
	((uint32_t)0x00000048UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x048 */
#define MXC_R_GPIO_WAKE_EN                                                \
	((uint32_t)0x0000004CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x04C */
#define MXC_R_GPIO_WAKE_EN_SET                                            \
	((uint32_t)0x00000050UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x050 */
#define MXC_R_GPIO_WAKE_EN_CLR                                            \
	((uint32_t)0x00000054UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x054 */
#define MXC_R_GPIO_INT_DUAL_EDGE                                          \
	((uint32_t)0x0000005CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x05C */
#define MXC_R_GPIO_PAD_CFG1                                               \
	((uint32_t)0x00000060UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x060 */
#define MXC_R_GPIO_PAD_CFG2                                               \
	((uint32_t)0x00000064UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x064 */
#define MXC_R_GPIO_EN1                                                    \
	((uint32_t)0x00000068UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x068 */
#define MXC_R_GPIO_EN1_SET                                                \
	((uint32_t)0x0000006CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x06C */
#define MXC_R_GPIO_EN1_CLR                                                \
	((uint32_t)0x00000070UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x070 */
#define MXC_R_GPIO_EN2                                                    \
	((uint32_t)0x00000074UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x074 */
#define MXC_R_GPIO_EN2_SET                                                \
	((uint32_t)0x00000078UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x078 */
#define MXC_R_GPIO_EN2_CLR                                                \
	((uint32_t)0x0000007CUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x07C */
#define MXC_R_GPIO_IS                                                     \
	((uint32_t)0x000000A8UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x0A8 */
#define MXC_R_GPIO_SR                                                     \
	((uint32_t)0x000000ACUL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x0AC */
#define MXC_R_GPIO_DS                                                     \
	((uint32_t)0x000000B0UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x0B0 */
#define MXC_R_GPIO_DS1                                                    \
	((uint32_t)0x000000B4UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x0B4 */
#define MXC_R_GPIO_PS                                                     \
	((uint32_t)0x000000B8UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x0B8 */
#define MXC_R_GPIO_VSSEL                                                  \
	((uint32_t)0x000000C0UL) /**< Offset from GPIO Base Address: <tt> \
				    0x0x0C0 */

/**
 * GPIO Function Enable Register. Each bit controls the GPIO_EN
 * setting for one GPIO pin on the associated port.
 */
#define MXC_F_GPIO_EN_GPIO_EN_POS 0 /**< EN_GPIO_EN Position */
#define MXC_F_GPIO_EN_GPIO_EN                                                 \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_EN_GPIO_EN_POS)) /**<          \
								   EN_GPIO_EN \
								   Mask */
#define MXC_V_GPIO_EN_GPIO_EN_ALTERNATE \
	((uint32_t)0x0UL) /**< EN_GPIO_EN_ALTERNATE Value */
#define MXC_S_GPIO_EN_GPIO_EN_ALTERNATE                        \
	(MXC_V_GPIO_EN_GPIO_EN_ALTERNATE                       \
	 << MXC_F_GPIO_EN_GPIO_EN_POS) /**<                    \
					  EN_GPIO_EN_ALTERNATE \
					  Setting              \
					*/
#define MXC_V_GPIO_EN_GPIO_EN_GPIO \
	((uint32_t)0x1UL) /**< EN_GPIO_EN_GPIO Value */
#define MXC_S_GPIO_EN_GPIO_EN_GPIO                        \
	(MXC_V_GPIO_EN_GPIO_EN_GPIO                       \
	 << MXC_F_GPIO_EN_GPIO_EN_POS) /**<               \
					  EN_GPIO_EN_GPIO \
					  Setting         \
					*/

/**
 * GPIO Set Function Enable Register. Writing a 1 to one or more bits
 * in this register sets the bits in the same positions in GPIO_EN to 1, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_EN_SET_ALL_POS 0 /**< EN_SET_ALL Position */
#define MXC_F_GPIO_EN_SET_ALL                                                 \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_EN_SET_ALL_POS)) /**<          \
								   EN_SET_ALL \
								   Mask */

/**
 * GPIO Clear Function Enable Register. Writing a 1 to one or more
 * bits in this register clears the bits in the same positions in GPIO_EN to 0,
 * without affecting other bits in that register.
 */
#define MXC_F_GPIO_EN_CLR_ALL_POS 0 /**< EN_CLR_ALL Position */
#define MXC_F_GPIO_EN_CLR_ALL                                                 \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_EN_CLR_ALL_POS)) /**<          \
								   EN_CLR_ALL \
								   Mask */

/**
 * GPIO Output Enable Register. Each bit controls the GPIO_OUT_EN
 * setting for one GPIO pin in the associated port.
 */
#define MXC_F_GPIO_OUT_EN_GPIO_OUT_EN_POS  \
	0 /**< OUT_EN_GPIO_OUT_EN Position \
	   */
#define MXC_F_GPIO_OUT_EN_GPIO_OUT_EN                                            \
	((uint32_t)(0xFFFFFFFFUL                                                 \
		    << MXC_F_GPIO_OUT_EN_GPIO_OUT_EN_POS)) /**<                  \
							      OUT_EN_GPIO_OUT_EN \
							      Mask */
#define MXC_V_GPIO_OUT_EN_GPIO_OUT_EN_DIS \
	((uint32_t)0x0UL) /**< OUT_EN_GPIO_OUT_EN_DIS Value */
#define MXC_S_GPIO_OUT_EN_GPIO_OUT_EN_DIS                                  \
	(MXC_V_GPIO_OUT_EN_GPIO_OUT_EN_DIS                                 \
	 << MXC_F_GPIO_OUT_EN_GPIO_OUT_EN_POS) /**< OUT_EN_GPIO_OUT_EN_DIS \
						  Setting */
#define MXC_V_GPIO_OUT_EN_GPIO_OUT_EN_EN \
	((uint32_t)0x1UL) /**< OUT_EN_GPIO_OUT_EN_EN Value */
#define MXC_S_GPIO_OUT_EN_GPIO_OUT_EN_EN                                  \
	(MXC_V_GPIO_OUT_EN_GPIO_OUT_EN_EN                                 \
	 << MXC_F_GPIO_OUT_EN_GPIO_OUT_EN_POS) /**< OUT_EN_GPIO_OUT_EN_EN \
						  Setting */

/**
 * GPIO Output Enable Set Function Enable Register. Writing a 1 to one
 * or more bits in this register sets the bits in the same positions in
 * GPIO_OUT_EN to 1, without affecting other bits in that register.
 */
#define MXC_F_GPIO_OUT_EN_SET_ALL_POS 0 /**< OUT_EN_SET_ALL Position */
#define MXC_F_GPIO_OUT_EN_SET_ALL                                        \
	((uint32_t)(0xFFFFFFFFUL                                         \
		    << MXC_F_GPIO_OUT_EN_SET_ALL_POS)) /**<              \
							  OUT_EN_SET_ALL \
							  Mask */

/**
 * GPIO Output Enable Clear Function Enable Register. Writing a 1 to
 * one or more bits in this register clears the bits in the same positions in
 * GPIO_OUT_EN to 0, without affecting other bits in that register.
 */
#define MXC_F_GPIO_OUT_EN_CLR_ALL_POS 0 /**< OUT_EN_CLR_ALL Position */
#define MXC_F_GPIO_OUT_EN_CLR_ALL                                        \
	((uint32_t)(0xFFFFFFFFUL                                         \
		    << MXC_F_GPIO_OUT_EN_CLR_ALL_POS)) /**<              \
							  OUT_EN_CLR_ALL \
							  Mask */

/**
 * GPIO Output Register. Each bit controls the GPIO_OUT setting for
 * one pin in the associated port.  This register can be written either
 * directly, or by using the GPIO_OUT_SET and GPIO_OUT_CLR registers.
 */
#define MXC_F_GPIO_OUT_GPIO_OUT_POS 0 /**< OUT_GPIO_OUT Position */
#define MXC_F_GPIO_OUT_GPIO_OUT                                      \
	((uint32_t)(0xFFFFFFFFUL                                     \
		    << MXC_F_GPIO_OUT_GPIO_OUT_POS)) /**<            \
							OUT_GPIO_OUT \
							Mask */
#define MXC_V_GPIO_OUT_GPIO_OUT_LOW \
	((uint32_t)0x0UL) /**< OUT_GPIO_OUT_LOW Value */
#define MXC_S_GPIO_OUT_GPIO_OUT_LOW                          \
	(MXC_V_GPIO_OUT_GPIO_OUT_LOW                         \
	 << MXC_F_GPIO_OUT_GPIO_OUT_POS) /**<                \
					    OUT_GPIO_OUT_LOW \
					    Setting          \
					  */
#define MXC_V_GPIO_OUT_GPIO_OUT_HIGH \
	((uint32_t)0x1UL) /**< OUT_GPIO_OUT_HIGH Value */
#define MXC_S_GPIO_OUT_GPIO_OUT_HIGH                          \
	(MXC_V_GPIO_OUT_GPIO_OUT_HIGH                         \
	 << MXC_F_GPIO_OUT_GPIO_OUT_POS) /**<                 \
					    OUT_GPIO_OUT_HIGH \
					    Setting           \
					  */

/**
 * GPIO Output Set. Writing a 1 to one or more bits in this register
 * sets the bits in the same positions in GPIO_OUT to 1, without affecting other
 * bits in that register.
 */
#define MXC_F_GPIO_OUT_SET_GPIO_OUT_SET_POS \
	0 /**< OUT_SET_GPIO_OUT_SET Position */
#define MXC_F_GPIO_OUT_SET_GPIO_OUT_SET                                              \
	((uint32_t)(0xFFFFFFFFUL                                                     \
		    << MXC_F_GPIO_OUT_SET_GPIO_OUT_SET_POS)) /**<                    \
								OUT_SET_GPIO_OUT_SET \
								Mask */
#define MXC_V_GPIO_OUT_SET_GPIO_OUT_SET_NO \
	((uint32_t)0x0UL) /**< OUT_SET_GPIO_OUT_SET_NO Value */
#define MXC_S_GPIO_OUT_SET_GPIO_OUT_SET_NO                                    \
	(MXC_V_GPIO_OUT_SET_GPIO_OUT_SET_NO                                   \
	 << MXC_F_GPIO_OUT_SET_GPIO_OUT_SET_POS) /**< OUT_SET_GPIO_OUT_SET_NO \
						    Setting */
#define MXC_V_GPIO_OUT_SET_GPIO_OUT_SET_SET \
	((uint32_t)0x1UL) /**< OUT_SET_GPIO_OUT_SET_SET Value */
#define MXC_S_GPIO_OUT_SET_GPIO_OUT_SET_SET                                    \
	(MXC_V_GPIO_OUT_SET_GPIO_OUT_SET_SET                                   \
	 << MXC_F_GPIO_OUT_SET_GPIO_OUT_SET_POS) /**< OUT_SET_GPIO_OUT_SET_SET \
						    Setting */
/**
 * GPIO Output Clear. Writing a 1 to one or more bits in this register
 * clears the bits in the same positions in GPIO_OUT to 0, without affecting
 * other bits in that register.
 */
#define MXC_F_GPIO_OUT_CLR_GPIO_OUT_CLR_POS \
	0 /**< OUT_CLR_GPIO_OUT_CLR Position */
#define MXC_F_GPIO_OUT_CLR_GPIO_OUT_CLR                                              \
	((uint32_t)(0xFFFFFFFFUL                                                     \
		    << MXC_F_GPIO_OUT_CLR_GPIO_OUT_CLR_POS)) /**<                    \
								OUT_CLR_GPIO_OUT_CLR \
								Mask */

/**
 * GPIO Input Register. Read-only register to read from the logic
 * states of the GPIO pins on this port.
 */
#define MXC_F_GPIO_IN_GPIO_IN_POS 0 /**< IN_GPIO_IN Position */
#define MXC_F_GPIO_IN_GPIO_IN                                                 \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_IN_GPIO_IN_POS)) /**<          \
								   IN_GPIO_IN \
								   Mask */

/**
 * GPIO Interrupt Mode Register. Each bit in this register controls
 * the interrupt mode setting for the associated GPIO pin on this port.
 */
#define MXC_F_GPIO_INT_MOD_GPIO_INT_MOD_POS \
	0 /**< INT_MOD_GPIO_INT_MOD Position */
#define MXC_F_GPIO_INT_MOD_GPIO_INT_MOD                                              \
	((uint32_t)(0xFFFFFFFFUL                                                     \
		    << MXC_F_GPIO_INT_MOD_GPIO_INT_MOD_POS)) /**<                    \
								INT_MOD_GPIO_INT_MOD \
								Mask */
#define MXC_V_GPIO_INT_MOD_GPIO_INT_MOD_LEVEL \
	((uint32_t)0x0UL) /**< INT_MOD_GPIO_INT_MOD_LEVEL Value */
#define MXC_S_GPIO_INT_MOD_GPIO_INT_MOD_LEVEL                                  \
	(MXC_V_GPIO_INT_MOD_GPIO_INT_MOD_LEVEL                                 \
	 << MXC_F_GPIO_INT_MOD_GPIO_INT_MOD_POS) /**<                          \
						    INT_MOD_GPIO_INT_MOD_LEVEL \
						    Setting */
#define MXC_V_GPIO_INT_MOD_GPIO_INT_MOD_EDGE \
	((uint32_t)0x1UL) /**< INT_MOD_GPIO_INT_MOD_EDGE Value */
#define MXC_S_GPIO_INT_MOD_GPIO_INT_MOD_EDGE                                  \
	(MXC_V_GPIO_INT_MOD_GPIO_INT_MOD_EDGE                                 \
	 << MXC_F_GPIO_INT_MOD_GPIO_INT_MOD_POS) /**<                         \
						    INT_MOD_GPIO_INT_MOD_EDGE \
						    Setting */

/**
 * GPIO Interrupt Polarity Register. Each bit in this register
 * controls the interrupt polarity setting for one GPIO pin in the associated
 * port.
 */
#define MXC_F_GPIO_INT_POL_GPIO_INT_POL_POS \
	0 /**< INT_POL_GPIO_INT_POL Position */
#define MXC_F_GPIO_INT_POL_GPIO_INT_POL                                              \
	((uint32_t)(0xFFFFFFFFUL                                                     \
		    << MXC_F_GPIO_INT_POL_GPIO_INT_POL_POS)) /**<                    \
								INT_POL_GPIO_INT_POL \
								Mask */
#define MXC_V_GPIO_INT_POL_GPIO_INT_POL_FALLING \
	((uint32_t)0x0UL) /**< INT_POL_GPIO_INT_POL_FALLING Value */
#define MXC_S_GPIO_INT_POL_GPIO_INT_POL_FALLING                                  \
	(MXC_V_GPIO_INT_POL_GPIO_INT_POL_FALLING                                 \
	 << MXC_F_GPIO_INT_POL_GPIO_INT_POL_POS) /**<                            \
						    INT_POL_GPIO_INT_POL_FALLING \
						    Setting */
#define MXC_V_GPIO_INT_POL_GPIO_INT_POL_RISING \
	((uint32_t)0x1UL) /**< INT_POL_GPIO_INT_POL_RISING Value */
#define MXC_S_GPIO_INT_POL_GPIO_INT_POL_RISING                                  \
	(MXC_V_GPIO_INT_POL_GPIO_INT_POL_RISING                                 \
	 << MXC_F_GPIO_INT_POL_GPIO_INT_POL_POS) /**<                           \
						    INT_POL_GPIO_INT_POL_RISING \
						    Setting */

/**
 * GPIO Interrupt Enable Register. Each bit in this register controls
 * the GPIO interrupt enable for the associated pin on the GPIO port.
 */
#define MXC_F_GPIO_INT_EN_GPIO_INT_EN_POS  \
	0 /**< INT_EN_GPIO_INT_EN Position \
	   */
#define MXC_F_GPIO_INT_EN_GPIO_INT_EN                                            \
	((uint32_t)(0xFFFFFFFFUL                                                 \
		    << MXC_F_GPIO_INT_EN_GPIO_INT_EN_POS)) /**<                  \
							      INT_EN_GPIO_INT_EN \
							      Mask */
#define MXC_V_GPIO_INT_EN_GPIO_INT_EN_DIS \
	((uint32_t)0x0UL) /**< INT_EN_GPIO_INT_EN_DIS Value */
#define MXC_S_GPIO_INT_EN_GPIO_INT_EN_DIS                                  \
	(MXC_V_GPIO_INT_EN_GPIO_INT_EN_DIS                                 \
	 << MXC_F_GPIO_INT_EN_GPIO_INT_EN_POS) /**< INT_EN_GPIO_INT_EN_DIS \
						  Setting */
#define MXC_V_GPIO_INT_EN_GPIO_INT_EN_EN \
	((uint32_t)0x1UL) /**< INT_EN_GPIO_INT_EN_EN Value */
#define MXC_S_GPIO_INT_EN_GPIO_INT_EN_EN                                  \
	(MXC_V_GPIO_INT_EN_GPIO_INT_EN_EN                                 \
	 << MXC_F_GPIO_INT_EN_GPIO_INT_EN_POS) /**< INT_EN_GPIO_INT_EN_EN \
						  Setting */

/**
 * GPIO Interrupt Enable Set. Writing a 1 to one or more bits in this
 * register sets the bits in the same positions in GPIO_INT_EN to 1, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_INT_EN_SET_GPIO_INT_EN_SET_POS \
	0 /**< INT_EN_SET_GPIO_INT_EN_SET Position */
#define MXC_F_GPIO_INT_EN_SET_GPIO_INT_EN_SET                                                    \
	((uint32_t)(0xFFFFFFFFUL                                                                 \
		    << MXC_F_GPIO_INT_EN_SET_GPIO_INT_EN_SET_POS)) /**<                          \
								      INT_EN_SET_GPIO_INT_EN_SET \
								      Mask */
#define MXC_V_GPIO_INT_EN_SET_GPIO_INT_EN_SET_NO \
	((uint32_t)0x0UL) /**< INT_EN_SET_GPIO_INT_EN_SET_NO Value */
#define MXC_S_GPIO_INT_EN_SET_GPIO_INT_EN_SET_NO                                        \
	(MXC_V_GPIO_INT_EN_SET_GPIO_INT_EN_SET_NO                                       \
	 << MXC_F_GPIO_INT_EN_SET_GPIO_INT_EN_SET_POS) /**<                             \
							  INT_EN_SET_GPIO_INT_EN_SET_NO \
							  Setting */
#define MXC_V_GPIO_INT_EN_SET_GPIO_INT_EN_SET_SET \
	((uint32_t)0x1UL) /**< INT_EN_SET_GPIO_INT_EN_SET_SET Value */
#define MXC_S_GPIO_INT_EN_SET_GPIO_INT_EN_SET_SET                                        \
	(MXC_V_GPIO_INT_EN_SET_GPIO_INT_EN_SET_SET                                       \
	 << MXC_F_GPIO_INT_EN_SET_GPIO_INT_EN_SET_POS) /**<                              \
							  INT_EN_SET_GPIO_INT_EN_SET_SET \
							  Setting */
/**
 * GPIO Interrupt Enable Clear. Writing a 1 to one or more bits in
 * this register clears the bits in the same positions in GPIO_INT_EN to 0,
 * without affecting other bits in that register.
 */
#define MXC_F_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_POS \
	0 /**< INT_EN_CLR_GPIO_INT_EN_CLR Position */
#define MXC_F_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR                                                    \
	((uint32_t)(0xFFFFFFFFUL                                                                 \
		    << MXC_F_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_POS)) /**<                          \
								      INT_EN_CLR_GPIO_INT_EN_CLR \
								      Mask */
#define MXC_V_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_NO \
	((uint32_t)0x0UL) /**< INT_EN_CLR_GPIO_INT_EN_CLR_NO Value */
#define MXC_S_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_NO                                        \
	(MXC_V_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_NO                                       \
	 << MXC_F_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_POS) /**<                             \
							  INT_EN_CLR_GPIO_INT_EN_CLR_NO \
							  Setting */
#define MXC_V_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_CLEAR \
	((uint32_t)0x1UL) /**< INT_EN_CLR_GPIO_INT_EN_CLR_CLEAR Value */
#define MXC_S_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_CLEAR                                        \
	(MXC_V_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_CLEAR                                       \
	 << MXC_F_GPIO_INT_EN_CLR_GPIO_INT_EN_CLR_POS) /**<                                \
							  INT_EN_CLR_GPIO_INT_EN_CLR_CLEAR \
							  Setting */
/**
 * GPIO Interrupt Status Register. Each bit in this register contains
 * the pending interrupt status for the associated GPIO pin in this port.
 */
#define MXC_F_GPIO_INT_STAT_GPIO_INT_STAT_POS \
	0 /**< INT_STAT_GPIO_INT_STAT Position */
#define MXC_F_GPIO_INT_STAT_GPIO_INT_STAT                                                \
	((uint32_t)(0xFFFFFFFFUL                                                         \
		    << MXC_F_GPIO_INT_STAT_GPIO_INT_STAT_POS)) /**<                      \
								  INT_STAT_GPIO_INT_STAT \
								  Mask */
#define MXC_V_GPIO_INT_STAT_GPIO_INT_STAT_NO \
	((uint32_t)0x0UL) /**< INT_STAT_GPIO_INT_STAT_NO Value */
#define MXC_S_GPIO_INT_STAT_GPIO_INT_STAT_NO                                    \
	(MXC_V_GPIO_INT_STAT_GPIO_INT_STAT_NO                                   \
	 << MXC_F_GPIO_INT_STAT_GPIO_INT_STAT_POS) /**<                         \
						      INT_STAT_GPIO_INT_STAT_NO \
						      Setting */
#define MXC_V_GPIO_INT_STAT_GPIO_INT_STAT_PENDING \
	((uint32_t)0x1UL) /**< INT_STAT_GPIO_INT_STAT_PENDING Value */
#define MXC_S_GPIO_INT_STAT_GPIO_INT_STAT_PENDING                                    \
	(MXC_V_GPIO_INT_STAT_GPIO_INT_STAT_PENDING                                   \
	 << MXC_F_GPIO_INT_STAT_GPIO_INT_STAT_POS) /**<                              \
						      INT_STAT_GPIO_INT_STAT_PENDING \
						      Setting */

/**
 * GPIO Status Clear. Writing a 1 to one or more bits in this register
 * clears the bits in the same positions in GPIO_INT_STAT to 0, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_INT_CLR_ALL_POS 0 /**< INT_CLR_ALL Position */
#define MXC_F_GPIO_INT_CLR_ALL                                     \
	((uint32_t)(0xFFFFFFFFUL                                   \
		    << MXC_F_GPIO_INT_CLR_ALL_POS)) /**<           \
						       INT_CLR_ALL \
						       Mask */

/**
 * GPIO Wake Enable Register. Each bit in this register controls the
 * PMU wakeup enable for the associated GPIO pin in this port.
 */
#define MXC_F_GPIO_WAKE_EN_GPIO_WAKE_EN_POS \
	0 /**< WAKE_EN_GPIO_WAKE_EN Position */
#define MXC_F_GPIO_WAKE_EN_GPIO_WAKE_EN                                              \
	((uint32_t)(0xFFFFFFFFUL                                                     \
		    << MXC_F_GPIO_WAKE_EN_GPIO_WAKE_EN_POS)) /**<                    \
								WAKE_EN_GPIO_WAKE_EN \
								Mask */
#define MXC_V_GPIO_WAKE_EN_GPIO_WAKE_EN_DIS \
	((uint32_t)0x0UL) /**< WAKE_EN_GPIO_WAKE_EN_DIS Value */
#define MXC_S_GPIO_WAKE_EN_GPIO_WAKE_EN_DIS                                    \
	(MXC_V_GPIO_WAKE_EN_GPIO_WAKE_EN_DIS                                   \
	 << MXC_F_GPIO_WAKE_EN_GPIO_WAKE_EN_POS) /**< WAKE_EN_GPIO_WAKE_EN_DIS \
						    Setting */
#define MXC_V_GPIO_WAKE_EN_GPIO_WAKE_EN_EN \
	((uint32_t)0x1UL) /**< WAKE_EN_GPIO_WAKE_EN_EN Value */
#define MXC_S_GPIO_WAKE_EN_GPIO_WAKE_EN_EN                                    \
	(MXC_V_GPIO_WAKE_EN_GPIO_WAKE_EN_EN                                   \
	 << MXC_F_GPIO_WAKE_EN_GPIO_WAKE_EN_POS) /**< WAKE_EN_GPIO_WAKE_EN_EN \
						    Setting */

/**
 * GPIO Wake Enable Set. Writing a 1 to one or more bits in this
 * register sets the bits in the same positions in GPIO_WAKE_EN to 1, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_WAKE_EN_SET_ALL_POS 0 /**< WAKE_EN_SET_ALL Position */
#define MXC_F_GPIO_WAKE_EN_SET_ALL                                           \
	((uint32_t)(0xFFFFFFFFUL                                             \
		    << MXC_F_GPIO_WAKE_EN_SET_ALL_POS)) /**< WAKE_EN_SET_ALL \
							   Mask */

/**
 * GPIO Wake Enable Clear. Writing a 1 to one or more bits in this
 * register clears the bits in the same positions in GPIO_WAKE_EN to 0, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_WAKE_EN_CLR_ALL_POS 0 /**< WAKE_EN_CLR_ALL Position */
#define MXC_F_GPIO_WAKE_EN_CLR_ALL                                           \
	((uint32_t)(0xFFFFFFFFUL                                             \
		    << MXC_F_GPIO_WAKE_EN_CLR_ALL_POS)) /**< WAKE_EN_CLR_ALL \
							   Mask */

/**
 * GPIO Interrupt Dual Edge Mode Register. Each bit in this register
 * selects dual edge mode for the associated GPIO pin in this port.
 */
#define MXC_F_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_POS \
	0 /**< INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE Position */
#define MXC_F_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE                                                          \
	((uint32_t)(0xFFFFFFFFUL                                                                             \
		    << MXC_F_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_POS)) /**<                                \
									    INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE \
									    Mask                             \
									  */
#define MXC_V_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_NO \
	((uint32_t)0x0UL) /**< INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_NO Value */
#define MXC_S_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_NO                                              \
	(MXC_V_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_NO                                             \
	 << MXC_F_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_POS) /**<                                   \
								INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_NO \
								Setting */
#define MXC_V_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_EN \
	((uint32_t)0x1UL) /**< INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_EN Value */
#define MXC_S_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_EN                                              \
	(MXC_V_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_EN                                             \
	 << MXC_F_GPIO_INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_POS) /**<                                   \
								INT_DUAL_EDGE_GPIO_INT_DUAL_EDGE_EN \
								Setting */

/**
 * GPIO Input Mode Config 1. Each bit in this register enables the
 * weak pull-up for the associated GPIO pin in this port.
 */
#define MXC_F_GPIO_PAD_CFG1_GPIO_PAD_CFG1_POS \
	0 /**< PAD_CFG1_GPIO_PAD_CFG1 Position */
#define MXC_F_GPIO_PAD_CFG1_GPIO_PAD_CFG1                                                \
	((uint32_t)(0xFFFFFFFFUL                                                         \
		    << MXC_F_GPIO_PAD_CFG1_GPIO_PAD_CFG1_POS)) /**<                      \
								  PAD_CFG1_GPIO_PAD_CFG1 \
								  Mask */
#define MXC_V_GPIO_PAD_CFG1_GPIO_PAD_CFG1_IMPEDANCE \
	((uint32_t)0x0UL) /**< PAD_CFG1_GPIO_PAD_CFG1_IMPEDANCE Value */
#define MXC_S_GPIO_PAD_CFG1_GPIO_PAD_CFG1_IMPEDANCE                                    \
	(MXC_V_GPIO_PAD_CFG1_GPIO_PAD_CFG1_IMPEDANCE                                   \
	 << MXC_F_GPIO_PAD_CFG1_GPIO_PAD_CFG1_POS) /**<                                \
						      PAD_CFG1_GPIO_PAD_CFG1_IMPEDANCE \
						      Setting */
#define MXC_V_GPIO_PAD_CFG1_GPIO_PAD_CFG1_PU \
	((uint32_t)0x1UL) /**< PAD_CFG1_GPIO_PAD_CFG1_PU Value */
#define MXC_S_GPIO_PAD_CFG1_GPIO_PAD_CFG1_PU                                    \
	(MXC_V_GPIO_PAD_CFG1_GPIO_PAD_CFG1_PU                                   \
	 << MXC_F_GPIO_PAD_CFG1_GPIO_PAD_CFG1_POS) /**<                         \
						      PAD_CFG1_GPIO_PAD_CFG1_PU \
						      Setting */
#define MXC_V_GPIO_PAD_CFG1_GPIO_PAD_CFG1_PD \
	((uint32_t)0x2UL) /**< PAD_CFG1_GPIO_PAD_CFG1_PD Value */
#define MXC_S_GPIO_PAD_CFG1_GPIO_PAD_CFG1_PD                                    \
	(MXC_V_GPIO_PAD_CFG1_GPIO_PAD_CFG1_PD                                   \
	 << MXC_F_GPIO_PAD_CFG1_GPIO_PAD_CFG1_POS) /**<                         \
						      PAD_CFG1_GPIO_PAD_CFG1_PD \
						      Setting */

/**
 * GPIO Input Mode Config 2. Each bit in this register enables the
 * weak pull-up for the associated GPIO pin in this port.
 */
#define MXC_F_GPIO_PAD_CFG2_GPIO_PAD_CFG2_POS \
	0 /**< PAD_CFG2_GPIO_PAD_CFG2 Position */
#define MXC_F_GPIO_PAD_CFG2_GPIO_PAD_CFG2                                                \
	((uint32_t)(0xFFFFFFFFUL                                                         \
		    << MXC_F_GPIO_PAD_CFG2_GPIO_PAD_CFG2_POS)) /**<                      \
								  PAD_CFG2_GPIO_PAD_CFG2 \
								  Mask */
#define MXC_V_GPIO_PAD_CFG2_GPIO_PAD_CFG2_IMPEDANCE \
	((uint32_t)0x0UL) /**< PAD_CFG2_GPIO_PAD_CFG2_IMPEDANCE Value */
#define MXC_S_GPIO_PAD_CFG2_GPIO_PAD_CFG2_IMPEDANCE                                    \
	(MXC_V_GPIO_PAD_CFG2_GPIO_PAD_CFG2_IMPEDANCE                                   \
	 << MXC_F_GPIO_PAD_CFG2_GPIO_PAD_CFG2_POS) /**<                                \
						      PAD_CFG2_GPIO_PAD_CFG2_IMPEDANCE \
						      Setting */
#define MXC_V_GPIO_PAD_CFG2_GPIO_PAD_CFG2_PU \
	((uint32_t)0x1UL) /**< PAD_CFG2_GPIO_PAD_CFG2_PU Value */
#define MXC_S_GPIO_PAD_CFG2_GPIO_PAD_CFG2_PU                                    \
	(MXC_V_GPIO_PAD_CFG2_GPIO_PAD_CFG2_PU                                   \
	 << MXC_F_GPIO_PAD_CFG2_GPIO_PAD_CFG2_POS) /**<                         \
						      PAD_CFG2_GPIO_PAD_CFG2_PU \
						      Setting */
#define MXC_V_GPIO_PAD_CFG2_GPIO_PAD_CFG2_PD \
	((uint32_t)0x2UL) /**< PAD_CFG2_GPIO_PAD_CFG2_PD Value */
#define MXC_S_GPIO_PAD_CFG2_GPIO_PAD_CFG2_PD                                    \
	(MXC_V_GPIO_PAD_CFG2_GPIO_PAD_CFG2_PD                                   \
	 << MXC_F_GPIO_PAD_CFG2_GPIO_PAD_CFG2_POS) /**<                         \
						      PAD_CFG2_GPIO_PAD_CFG2_PD \
						      Setting */

/**
 * GPIO Alternate Function Enable Register. Each bit in this register
 * selects between primary/secondary functions for the associated GPIO pin in
 * this port.
 */
#define MXC_F_GPIO_EN1_GPIO_EN1_POS 0 /**< EN1_GPIO_EN1 Position */
#define MXC_F_GPIO_EN1_GPIO_EN1                                      \
	((uint32_t)(0xFFFFFFFFUL                                     \
		    << MXC_F_GPIO_EN1_GPIO_EN1_POS)) /**<            \
							EN1_GPIO_EN1 \
							Mask */
#define MXC_V_GPIO_EN1_GPIO_EN1_PRIMARY \
	((uint32_t)0x0UL) /**< EN1_GPIO_EN1_PRIMARY Value */
#define MXC_S_GPIO_EN1_GPIO_EN1_PRIMARY                          \
	(MXC_V_GPIO_EN1_GPIO_EN1_PRIMARY                         \
	 << MXC_F_GPIO_EN1_GPIO_EN1_POS) /**<                    \
					    EN1_GPIO_EN1_PRIMARY \
					    Setting              \
					  */
#define MXC_V_GPIO_EN1_GPIO_EN1_SECONDARY \
	((uint32_t)0x1UL) /**< EN1_GPIO_EN1_SECONDARY Value */
#define MXC_S_GPIO_EN1_GPIO_EN1_SECONDARY                                    \
	(MXC_V_GPIO_EN1_GPIO_EN1_SECONDARY                                   \
	 << MXC_F_GPIO_EN1_GPIO_EN1_POS) /**< EN1_GPIO_EN1_SECONDARY Setting \
					  */

/**
 * GPIO Alternate Function Set. Writing a 1 to one or more bits in
 * this register sets the bits in the same positions in GPIO_EN1 to 1, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_EN1_SET_ALL_POS 0 /**< EN1_SET_ALL Position */
#define MXC_F_GPIO_EN1_SET_ALL                                     \
	((uint32_t)(0xFFFFFFFFUL                                   \
		    << MXC_F_GPIO_EN1_SET_ALL_POS)) /**<           \
						       EN1_SET_ALL \
						       Mask */

/**
 * GPIO Alternate Function Clear. Writing a 1 to one or more bits in
 * this register clears the bits in the same positions in GPIO_EN1 to 0, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_EN1_CLR_ALL_POS 0 /**< EN1_CLR_ALL Position */
#define MXC_F_GPIO_EN1_CLR_ALL                                     \
	((uint32_t)(0xFFFFFFFFUL                                   \
		    << MXC_F_GPIO_EN1_CLR_ALL_POS)) /**<           \
						       EN1_CLR_ALL \
						       Mask */

/**
 * GPIO Alternate Function Enable Register. Each bit in this register
 * selects between primary/secondary functions for the associated GPIO pin in
 * this port.
 */
#define MXC_F_GPIO_EN2_GPIO_EN2_POS 0 /**< EN2_GPIO_EN2 Position */
#define MXC_F_GPIO_EN2_GPIO_EN2                                      \
	((uint32_t)(0xFFFFFFFFUL                                     \
		    << MXC_F_GPIO_EN2_GPIO_EN2_POS)) /**<            \
							EN2_GPIO_EN2 \
							Mask */
#define MXC_V_GPIO_EN2_GPIO_EN2_PRIMARY \
	((uint32_t)0x0UL) /**< EN2_GPIO_EN2_PRIMARY Value */
#define MXC_S_GPIO_EN2_GPIO_EN2_PRIMARY                          \
	(MXC_V_GPIO_EN2_GPIO_EN2_PRIMARY                         \
	 << MXC_F_GPIO_EN2_GPIO_EN2_POS) /**<                    \
					    EN2_GPIO_EN2_PRIMARY \
					    Setting              \
					  */
#define MXC_V_GPIO_EN2_GPIO_EN2_SECONDARY \
	((uint32_t)0x1UL) /**< EN2_GPIO_EN2_SECONDARY Value */
#define MXC_S_GPIO_EN2_GPIO_EN2_SECONDARY                                    \
	(MXC_V_GPIO_EN2_GPIO_EN2_SECONDARY                                   \
	 << MXC_F_GPIO_EN2_GPIO_EN2_POS) /**< EN2_GPIO_EN2_SECONDARY Setting \
					  */

/**
 * GPIO Alternate Function 2 Set. Writing a 1 to one or more bits in
 * this register sets the bits in the same positions in GPIO_EN2 to 1, without
 * affecting other bits in that register.
 */
#define MXC_F_GPIO_EN2_SET_ALL_POS 0 /**< EN2_SET_ALL Position */
#define MXC_F_GPIO_EN2_SET_ALL                                     \
	((uint32_t)(0xFFFFFFFFUL                                   \
		    << MXC_F_GPIO_EN2_SET_ALL_POS)) /**<           \
						       EN2_SET_ALL \
						       Mask */

/**
 * GPIO Wake Alternate Function Clear. Writing a 1 to one or more bits
 * in this register clears the bits in the same positions in GPIO_EN2 to 0,
 * without affecting other bits in that register.
 */
#define MXC_F_GPIO_EN2_CLR_ALL_POS 0 /**< EN2_CLR_ALL Position */
#define MXC_F_GPIO_EN2_CLR_ALL                                     \
	((uint32_t)(0xFFFFFFFFUL                                   \
		    << MXC_F_GPIO_EN2_CLR_ALL_POS)) /**<           \
						       EN2_CLR_ALL \
						       Mask */

/**
 * GPIO Drive Strength  Register. Each bit in this register selects
 * the drive strength for the associated GPIO pin in this port. Refer to the
 * Datasheet for sink/source current of GPIO pins in each mode.
 */
#define MXC_F_GPIO_DS_DS_POS 0 /**< DS_DS Position */
#define MXC_F_GPIO_DS_DS \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_DS_DS_POS)) /**< DS_DS Mask */
#define MXC_V_GPIO_DS_DS_LD ((uint32_t)0x0UL) /**< DS_DS_LD Value */
#define MXC_S_GPIO_DS_DS_LD \
	(MXC_V_GPIO_DS_DS_LD << MXC_F_GPIO_DS_DS_POS) /**< DS_DS_LD Setting */
#define MXC_V_GPIO_DS_DS_HD ((uint32_t)0x1UL) /**< DS_DS_HD Value */
#define MXC_S_GPIO_DS_DS_HD \
	(MXC_V_GPIO_DS_DS_HD << MXC_F_GPIO_DS_DS_POS) /**< DS_DS_HD Setting */

/**
 * GPIO Drive Strength 1 Register. Each bit in this register selects
 * the drive strength for the associated GPIO pin in this port. Refer to the
 * Datasheet for sink/source current of GPIO pins in each mode.
 */
#define MXC_F_GPIO_DS1_ALL_POS 0 /**< DS1_ALL Position */
#define MXC_F_GPIO_DS1_ALL                                                     \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_DS1_ALL_POS)) /**< DS1_ALL Mask \
							      */

/**
 * GPIO Pull Select Mode.
 */
#define MXC_F_GPIO_PS_ALL_POS 0 /**< PS_ALL Position */
#define MXC_F_GPIO_PS_ALL                                                    \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_PS_ALL_POS)) /**< PS_ALL Mask \
							     */

/**
 * GPIO Voltage Select.
 */
#define MXC_F_GPIO_VSSEL_ALL_POS 0 /**< VSSEL_ALL Position */
#define MXC_F_GPIO_VSSEL_ALL                                                  \
	((uint32_t)(0xFFFFFFFFUL << MXC_F_GPIO_VSSEL_ALL_POS)) /**< VSSEL_ALL \
								  Mask */

#endif /* _GPIO_REGS_H_ */
