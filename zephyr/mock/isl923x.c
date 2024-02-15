/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/isl923x.h"

enum ec_error_list raa489000_is_acok_absent(int charger, bool *acok)
{
	*acok = false;
	return EC_SUCCESS;
}

enum ec_error_list raa489000_is_acok_present(int charger, bool *acok)
{
	*acok = true;
	return EC_SUCCESS;
}

enum ec_error_list raa489000_is_acok_error(int charger, bool *acok)
{
	return EC_ERROR_UNIMPLEMENTED;
}
