/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "flash.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

extern const char *flash_physical_dataptr_override;

static char flash[CONFIG_FLASH_SIZE_BYTES];

static void after(void *f)
{
	ARG_UNUSED(f);
	flash_physical_dataptr_override = NULL;
}

ZTEST_SUITE(memmap, drivers_predicate_post_main, NULL, NULL, after, NULL);

ZTEST(memmap, test_crec_flash_dataptr__invalid)
{
	zassert_equal(-1, crec_flash_dataptr(/*offset=*/-1, /*size_req=*/1,
					     /*align=*/1, /*ptrp=*/NULL));
}

ZTEST(memmap, test_crec_flash_dataptr)
{
	const char *ptr = NULL;

	zassert_equal(CONFIG_PLATFORM_EC_FLASH_SIZE_BYTES,
		      crec_flash_dataptr(0, 1, 1, &ptr));
	zassert_equal(CONFIG_PLATFORM_EC_MAPPED_STORAGE_BASE, (uintptr_t)ptr);
}

ZTEST(memmap, test_crec_flash_is_erased__invalid_args)
{
	zassert_equal(0, crec_flash_is_erased(/*offset=*/0, /*size=*/-1));
}

ZTEST(memmap, test_crec_flash_is_erased__fail)
{
	sprintf(flash, "non empty data");
	flash_physical_dataptr_override = flash;
	zassert_equal(0, crec_flash_is_erased(/*offset=*/0, /*size=*/8));
}

ZTEST(memmap, test_crec_flash_is_erased__pass)
{
	memset(flash, 0xff, 32);
	flash_physical_dataptr_override = flash;
	zassert_equal(1, crec_flash_is_erased(/*offset=*/0, /*size=*/32));
}

ZTEST(memmap, test_crec_flash_read__invalid_args)
{
	zassert_equal(EC_ERROR_INVAL, crec_flash_read(/*offset=*/-1, /*size=*/0,
						      /*data=*/NULL));
}

ZTEST(memmap, test_crec_flash_read)
{
	char output[16] = { 0 };

	sprintf(flash, "0123456789abcdef");
	flash_physical_dataptr_override = flash;

	zassert_ok(crec_flash_read(/*offset=*/0, ARRAY_SIZE(output), output));
	zassert_mem_equal(output, flash, ARRAY_SIZE(output));
}

ZTEST(memmap, test_crec_flash_write__invalid_args)
{
	zassert_equal(EC_ERROR_INVAL,
		      crec_flash_write(/*offset=*/-1, /*size=*/0,
				       /*data=*/NULL));
}

ZTEST(memmap, test_crec_flash_erase__invalid_args)
{
	zassert_equal(EC_ERROR_INVAL,
		      crec_flash_erase(/*offset=*/-1, /*size=*/0));
}
