/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

/* This test requires both flash_simulator and CONFIG_MAPPED_STORAGE enabled
 * and synced.
 *
 * MAPPED_STORAGE_BASE must be hard-coded in the config file, but
 * flash_simulator allocates memory at runtime.
 * To solve this, the following code opens the simulated flash file at a
 * hard-coded address (currently 0x800000, this address is currently unused,
 * the test program only uses memory up to ~0x500000).
 *
 * Users can use msync(2) to sync data between flash_simulator and
 * MAPPED_STORAGE if needed.
 */
void test_main(void)
{
	int fd = open("flash.bin", O_RDWR);

	zassert_not_equal(fd, -1);

	/* May fail if MAPPED_STORAGE_BASE(=0x800000) is occupied.
	 * Move MAPPED_STORAGE_BASE to other address if failed.
	 */
	void *ptr = mmap((void *)CONFIG_PLATFORM_EC_MAPPED_STORAGE_BASE,
			 1048576, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);

	zassert_equal((intptr_t)ptr, CONFIG_PLATFORM_EC_MAPPED_STORAGE_BASE);

	ztest_run_all(NULL, false, 1, 1);

	munmap(ptr, 1048576);
	close(fd);
}
