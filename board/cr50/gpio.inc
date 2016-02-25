/* -*- mode:c -*-
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about */
/* Inputs with interrupt handlers are first for efficiency */


/* Unimplemented signals which we need to emulate for now */
/* TODO(wfrichar): Half the boards don't use this signal. Take it out. */
UNIMPLEMENTED(ENTERING_RW)

/*
 * If we are included by generic GPIO code that doesn't know about the PINMUX
 * macro we need to provide an empty definition so that the invocations don't
 * interfere with other GPIO processing.
 */
#ifndef PINMUX
#define PINMUX(...)
#endif

/* The serial port is one of the SoC peripheral functions */
PINMUX(FUNC(UART0_TX), A0, DIO_OUTPUT)		/* bootrom sets this already */
PINMUX(FUNC(UART0_RX), A1, DIO_INPUT)		/* we need to set this */

#undef PINMUX
