/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/charger/isl923x_public.h"
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

__weak int isl923x_set_ac_prochot(int chgnum, uint16_t ma)
{
	return EC_ERROR_UNIMPLEMENTED;
}

__weak int isl923x_set_dc_prochot(int chgnum, uint16_t ma)
{
	return EC_ERROR_UNIMPLEMENTED;
}
