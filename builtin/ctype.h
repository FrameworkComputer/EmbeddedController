/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CTYPE_H__
#define __CROS_EC_CTYPE_H__

int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int isupper(int c);
int isprint(int c);
int tolower(int c);

#endif /* __CROS_EC_CTYPE_H__ */
