/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef CHIP_STM32_USART_STM32F3_H
#define CHIP_STM32_USART_STM32F3_H

#include "usart.h"

#define STM32_USARTS_MAX 3

/*
 * The STM32F3 series can have as many as three UARTS.  These are the HW configs
 * for those UARTS.  They can be used to initialize STM32 generic UART configs.
 */
extern struct usart_hw_config const usart1_hw;
extern struct usart_hw_config const usart2_hw;
extern struct usart_hw_config const usart3_hw;

#endif /* CHIP_STM32_USART_STM32F3_H */
