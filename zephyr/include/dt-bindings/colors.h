/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef DT_BINDINGS_COLORS_H_
#define DT_BINDINGS_COLORS_H_

/*
 * Taken from ec_command.h
 * This must match enum ec_led_colors from ec_command.h
 * Used to map the EC host command brightness requests
 * to the appropriate LED output.
 */
#define	RED	0
#define	GREEN	1
#define	BLUE	2
#define	YELLOW	3
#define	WHITE	4
#define	AMBER	5


#endif /* DT_BINDINGS_COLORS_H_ */
