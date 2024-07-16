/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"

#include <drivers/vivaldi_kbd.h>

/*
 * We have total 32 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { -1, -1 }, { -1, -1 }, { 3, 1 },  { 3, 0 }, { 2, 7 },
	{ 2, 6 },   { 2, 5 },	{ 2, 4 },   { 2, 3 },  { 2, 2 }, { 8, 3 },
	{ 8, 2 },   { 0, 4 },	{ 0, 5 },   { 0, 6 },  { 0, 7 }, { 1, 0 },
	{ 1, 1 },   { 1, 2 },	{ 1, 3 },   { 1, 4 },  { 1, 5 }, { 1, 6 },
	{ 1, 7 },   { 2, 0 },	{ 2, 1 },   { 11, 1 }, { 0, 3 }, { -1, -1 },
	{ -1, -1 }, { -1, -1 }, { -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
