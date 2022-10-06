/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USART_STM32L5_H
#define __CROS_EC_USART_STM32L5_H

#include "usart.h"

/*
 * The STM32L5 series can have as many as five UARTS.  These are the HW configs
 * for those UARTS.  They can be used to initialize STM32 generic UART configs.
 */
extern struct usart_hw_config const usart1_hw;
extern struct usart_hw_config const usart2_hw;
extern struct usart_hw_config const usart3_hw;
extern struct usart_hw_config const usart4_hw;
extern struct usart_hw_config const usart5_hw;
extern struct usart_hw_config const usart9_hw; /* LPUART1 */

#endif /* __CROS_EC_USART_STM32L5_H */
