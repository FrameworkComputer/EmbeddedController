/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

ZTEST_SUITE(host_cmd_get_version, NULL, NULL, NULL, NULL, NULL);

__override const char *system_get_version(enum ec_image copy)
{
	switch (copy) {
	case EC_IMAGE_RO:
		return "version-ro";
	case EC_IMAGE_RW:
		return "version-rw";
	default:
		return "unknown";
	}
}

ZTEST(host_cmd_get_version, test_get_version_v1)
{
	int ret;
	struct ec_response_get_version_v1 r;
	struct host_cmd_handler_args args;

	ret = ec_cmd_get_version_v1(&args, &r);

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);

	zassert_equal(strcmp(r.version_string_ro, "version-ro"), 0,
		      "version_string_ro: %s", r.version_string_ro);
	zassert_equal(args.response_size, sizeof(r), "response_size: %d",
		      args.response_size);
	zassert_equal(strcmp(r.version_string_rw, "version-rw"), 0,
		      "version_string_rw: %s", r.version_string_rw);
	zassert_equal(strcmp(r.cros_fwid_ro, ""), 0, "cros_fwid_ro: %s",
		      r.cros_fwid_ro);
	zassert_equal(strcmp(r.cros_fwid_rw, ""), 0, "cros_fwid_ro: %s",
		      r.cros_fwid_rw);
	zassert_equal(r.current_image, EC_IMAGE_UNKNOWN, "current_image: %s",
		      r.current_image);
}

ZTEST(host_cmd_get_version, test_get_version_v0)
{
	int ret;
	struct ec_response_get_version r;
	struct host_cmd_handler_args args;

	ret = ec_cmd_get_version(&args, &r);

	zassert_equal(ret, EC_SUCCESS, "Unexpected return value: %d", ret);

	zassert_equal(strcmp(r.version_string_ro, "version-ro"), 0,
		      "version_string_ro: %s", r.version_string_ro);
	zassert_equal(args.response_size, sizeof(r), "response_size: %d",
		      args.response_size);
	zassert_equal(strcmp(r.version_string_rw, "version-rw"), 0,
		      "version_string_rw: %s", r.version_string_rw);
	zassert_equal(r.current_image, EC_IMAGE_UNKNOWN, "current_image: %s",
		      r.current_image);
}
