/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "link_defs.h"
#include "multistep_test.h"
#include "ram_lock.h"

#include <zephyr/ztest.h>

struct mpu_rw_regions {
	int num_regions;
	uint32_t addr;
	uint32_t size;
};

static int write_succeeds(uint32_t addr)
{
	*(volatile uint32_t *)addr = addr;

	if (*(volatile uint32_t *)addr != addr)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int verify_no_write(uint32_t addr)
{
	zassert_equal(write_succeeds(addr), EC_ERROR_UNKNOWN);
	return EC_SUCCESS;
}

static int verify_write(uint32_t addr)
{
	zassert_equal(write_succeeds(addr), EC_SUCCESS);
	return EC_SUCCESS;
}

#if defined(CONFIG_SOC_NPCX9MFP)
/* Used to map to alias data ram */
#define ALIAS_DATA_RAM_SHIFT 0x10000000

/* Set a part of data RAM to fetch protection and use to check this region can
 * be written */
struct mpu_rw_regions data_ram_1 = { .num_regions = REGION_DATA_RAM,
				     .addr = CONFIG_CROS_EC_RAM_BASE,
				     .size = 0x3000 };

/* Set a part of data RAM to write protection and use to check this region
 * cannot be written */
struct mpu_rw_regions data_ram_2 = { .num_regions = REGION_STORAGE,
				     .addr = (uint32_t)__shared_mem_buf,
				     .size = 0x3000 };

/* Use to check the protection region cannot be set because the address is not
 * aligned */
struct mpu_rw_regions invalid_code_reg_addr_not_aligned = {
	.num_regions = REGION_STORAGE,
	.addr = 0x10059AB1,
	.size = 0x3000
};

/* Use to check the protection region cannot be set because the size is not
 * aligned */
struct mpu_rw_regions invalid_code_reg_size_not_aligned = {
	.num_regions = REGION_STORAGE,
	.addr = 0x10058000,
	.size = 0x3A80
};

/* Set the fetch-protect region and use to check this region cannot be fetched
 */
struct mpu_rw_regions fetch_lock_ram = { .num_regions = REGION_DATA_RAM,
					 .addr = (uint32_t)__shared_mem_buf,
					 .size = 0x2000 };
#else
#error "MPU info not defined for this chip. Please add it."
#endif

/*
 * Check the setting function will return error when the address is not
 * 4K aligned.
 */
static int test_ram_lock_config_lock_region_invalid_addr(void)
{
	zassert_equal(ram_lock_config_lock_region(
			      invalid_code_reg_addr_not_aligned.num_regions,
			      invalid_code_reg_addr_not_aligned.addr,
			      invalid_code_reg_addr_not_aligned.size),
		      -EINVAL);

	return EC_SUCCESS;
}

/*
 * Check the setting function will return error when the size is not
 * 4K aligned.
 */
static int test_ram_lock_config_lock_region_invalid_size(void)
{
	zassert_equal(ram_lock_config_lock_region(
			      invalid_code_reg_size_not_aligned.num_regions,
			      invalid_code_reg_size_not_aligned.addr,
			      invalid_code_reg_size_not_aligned.size),
		      -EINVAL);

	return EC_SUCCESS;
}

/* Set a part of the region as a protection area and return success */
static int test_ram_lock_config_lock_region(void)
{
	zassert_equal(ram_lock_config_lock_region(REGION_DATA_RAM,
						  CONFIG_CROS_EC_RAM_BASE,
						  0x10000),
		      EC_SUCCESS);
	zassert_equal(
		ram_lock_config_lock_region(REGION_STORAGE,
					    CONFIG_CROS_EC_PROGRAM_MEMORY_BASE +
						    CONFIG_CROS_EC_RO_MEM_OFF,
					    0x10000),
		EC_SUCCESS);

	return EC_SUCCESS;
}

/* Check the fetch-protect region can be written and the write-protect region
 * cannot be written */
static int test_ram_write_protect(void)
{
	zassert_equal(verify_no_write(CONFIG_CROS_EC_PROGRAM_MEMORY_BASE +
				      CONFIG_CROS_EC_RO_MEM_OFF),
		      EC_SUCCESS);
	zassert_equal(verify_write(CONFIG_CROS_EC_RAM_BASE), EC_SUCCESS);

	return EC_SUCCESS;
}

/* Set a part of the region as a protection area and return success */
static int test_ram_lock_config_lock_region_alias(void)
{
	zassert_equal(ram_lock_config_lock_region(data_ram_1.num_regions,
						  data_ram_1.addr,
						  data_ram_1.size),
		      EC_SUCCESS);

	/* Set protection region with 4K aligned address and map to alias data
	 * ram */
	data_ram_2.addr =
		(data_ram_2.addr & ~0xFFF) + 0x1000 - ALIAS_DATA_RAM_SHIFT;

	zassert_equal(ram_lock_config_lock_region(data_ram_2.num_regions,
						  data_ram_2.addr,
						  data_ram_2.size),
		      EC_SUCCESS);

	return EC_SUCCESS;
}

/* Check the fetch-protect region can be written and the write-protect region
 * cannot be written */
static int test_ram_alias_write_protect(void)
{
	zassert_equal(verify_write(data_ram_1.addr), EC_SUCCESS);
	zassert_equal(verify_no_write(data_ram_2.addr), EC_SUCCESS);

	return EC_SUCCESS;
}

/* Check the fetch-protect region cannot be fetched */
static int test_ram_fetch_protect(uint32_t addr)
{
	uintptr_t __ram_test_addr = addr;
	int (*__test_fptr)(void) = (int (*)(void))(__ram_test_addr | 0x01);

	/*
	 * Assembly for the following test function:
	 *
	 *  int test_function()
	 * {
	 *	return EC_SUCCESS;
	 * }
	 */
	uint16_t test_function[] = {
		0x2000, /* movs    r0, #0x0 */
		0x4770, /* bx      lr       */
	};

	/* Copy test_function to assigned address */
	memcpy((void *)__ram_test_addr, test_function, sizeof(test_function));

	/* Execute instruction and it can be run */
	zassert_equal(__test_fptr(), EC_SUCCESS);

	/* Set the protection region for fetch operation */
	zassert_equal(ram_lock_config_lock_region(fetch_lock_ram.num_regions,
						  fetch_lock_ram.addr,
						  fetch_lock_ram.size),
		      EC_SUCCESS);

	/* Execute instruction and it will cause busfault and reboot */
	zassert_equal(__test_fptr(), EC_SUCCESS);

	return EC_SUCCESS;
}

/* Test fetch lock in data ram */
static int test_data_ram_fetch(void)
{
	fetch_lock_ram.addr = (fetch_lock_ram.addr & ~0xFFF) + 0x1000;

	return test_ram_fetch_protect(fetch_lock_ram.addr);
}

/* Test fetch lock in alias data ram */
static int test_alias_data_ram_fetch(void)
{
	fetch_lock_ram.addr = (fetch_lock_ram.addr & ~0xFFF) + 0x1000;

	return test_ram_fetch_protect(fetch_lock_ram.addr -
				      ALIAS_DATA_RAM_SHIFT);
}

static void test_step1_ram_lock(void)
{
	test_data_ram_fetch();
	zassert_unreachable();
}

static void test_step2_ram_lock(void)
{
	test_alias_data_ram_fetch();
	zassert_unreachable();
}

static void test_step3_ram_lock(void)
{
	zassert_equal(test_ram_lock_config_lock_region_invalid_addr(),
		      EC_SUCCESS);
	zassert_equal(test_ram_lock_config_lock_region_invalid_size(),
		      EC_SUCCESS);
	zassert_equal(test_ram_lock_config_lock_region(), EC_SUCCESS);
	zassert_equal(test_ram_write_protect(), EC_SUCCESS);
	zassert_equal(test_ram_lock_config_lock_region_alias(), EC_SUCCESS);
	zassert_equal(test_ram_alias_write_protect(), EC_SUCCESS);
}

static void (*test_steps[])(void) = { test_step1_ram_lock, test_step2_ram_lock,
				      test_step3_ram_lock };

MULTISTEP_TEST(ram_lock, test_steps)
