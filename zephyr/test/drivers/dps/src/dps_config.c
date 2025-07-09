/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/stdio.h"
#include "console.h"
#include "dps.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <zephyr/ztest.h>

struct dps_config_fixture {
	struct dps_config_t saved_config;
	int saved_debug_level;
};

static void *dps_config_setup(void)
{
	static struct dps_config_fixture fixture;

	fixture.saved_config = *dps_get_config();
	fixture.saved_debug_level = *dps_get_debug_level();

	return &fixture;
}

static void dps_config_before(void *data)
{
	dps_enable(true);
}

static void dps_config_after(void *data)
{
	struct dps_config_fixture *f = (struct dps_config_fixture *)data;

	*dps_get_config() = f->saved_config;
	*dps_get_debug_level() = f->saved_debug_level;
	dps_enable(true);
}

ZTEST_F(dps_config, test_enable)
{
	zassert_true(dps_is_enabled(), NULL);
	dps_enable(false);
	zassert_false(dps_is_enabled(), NULL);
	dps_enable(true);
	zassert_true(dps_is_enabled(), NULL);
}

ZTEST_F(dps_config, test_config)
{
	struct dps_config_t *config = dps_get_config();

	zassert_true(config->k_less_pwr <= config->k_more_pwr, NULL);
	zassert_true(config->k_less_pwr > 0 && config->k_less_pwr < 100, NULL);
	zassert_true(config->k_more_pwr > 0 && config->k_more_pwr < 100, NULL);

	zassert_ok(dps_init(), NULL);
	*config = fixture->saved_config;

	config->k_less_pwr = config->k_more_pwr + 1;
	zassert_equal(dps_init(), EC_ERROR_INVALID_CONFIG, NULL);
	*config = fixture->saved_config;

	config->k_more_pwr = 101;
	zassert_equal(dps_init(), EC_ERROR_INVALID_CONFIG, NULL);
	*config = fixture->saved_config;
}

ZTEST(dps_config, test_console_cmd__print_info)
{
	/* Print current status to console */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps"), NULL);
}

ZTEST(dps_config, test_console_cmd__enable)
{
	/* Disable DPS first, then try enabling */
	dps_enable(false);
	zassert_false(dps_is_enabled(), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps en"), NULL);

	zassert_true(dps_is_enabled(), NULL);
}

ZTEST(dps_config, test_console_cmd__disable)
{
	/* Should already by enabled due to before() function */
	zassert_true(dps_is_enabled(), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps dis"), NULL);

	zassert_false(dps_is_enabled(), NULL);
}

ZTEST(dps_config, test_console_cmd__fakepwr_print)
{
	/* Print current fake power status to console */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps fakepwr"), NULL);
}

ZTEST(dps_config, test_console_cmd__fakepwr_enable_disable)
{
	zassert_false(dps_is_fake_enabled(),
		      "fakepwr shouldn't be enabled by default");

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps fakepwr 100 200"),
		   NULL);
	zassert_true(dps_is_fake_enabled(), NULL);
	zassert_equal(100, dps_get_fake_mv(), "Got fake_mv=%d",
		      dps_get_fake_mv());
	zassert_equal(200, dps_get_fake_ma(), "Got fake_ma=%d",
		      dps_get_fake_ma());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps fakepwr dis"), NULL);
	zassert_false(dps_is_fake_enabled(), NULL);
}

ZTEST(dps_config, test_console_cmd__fakepwr_invalid)
{
	/* Various invalid parameters */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps fakepwr 100"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps fakepwr -100 -200"),
		   NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps fakepwr 100 -200"),
		   NULL);
}

ZTEST(dps_config, test_console_cmd__debuglevel)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps debug 999"), NULL);

	zassert_equal(999, *dps_get_debug_level(), "Debug level is %d",
		      *dps_get_debug_level());
}

ZTEST(dps_config, test_console_cmd__setkmore)
{
	struct dps_config_t *config = dps_get_config();
	char cmd[32];

	/* Try some invalid requests first */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkmore"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkmore 101"),
		   NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkmore 0"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkmore -1"), NULL);

	zassert_true(crec_snprintf(cmd, sizeof(cmd), "dps setkmore %d",
				   config->k_less_pwr - 1) > 0,
		     NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), cmd), NULL);

	/* Adjust k_more_pwr to be one over k_less_pwr */
	zassert_true(crec_snprintf(cmd, sizeof(cmd), "dps setkmore %d",
				   config->k_less_pwr + 1) > 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);

	zassert_equal(config->k_less_pwr + 1, config->k_more_pwr,
		      "k_more_pwr is %d but should be %d", config->k_more_pwr,
		      config->k_less_pwr + 1);
}

ZTEST(dps_config, test_console_cmd__setkless)
{
	struct dps_config_t *config = dps_get_config();
	char cmd[32];

	/* Try some invalid requests first */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkless"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkless 101"),
		   NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkless 0"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkless -1"), NULL);

	zassert_true(crec_snprintf(cmd, sizeof(cmd), "dps setkless %d",
				   config->k_more_pwr + 1) > 0,
		     NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), cmd), NULL);

	/* Adjust k_less_pwr to be one under k_more_pwr */
	zassert_true(crec_snprintf(cmd, sizeof(cmd), "dps setkless %d",
				   config->k_more_pwr - 1) > 0,
		     NULL);
	zassert_ok(shell_execute_cmd(get_ec_shell(), cmd), NULL);

	zassert_equal(config->k_more_pwr - 1, config->k_less_pwr,
		      "k_less_pwr is %d but should be %d", config->k_less_pwr,
		      config->k_more_pwr - 1);
}

ZTEST(dps_config, test_console_cmd__setksample)
{
	struct dps_config_t *config = dps_get_config();

	/* Try some invalid requests first */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setksample"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setksample -1"),
		   NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps setksample 999"),
		   NULL);

	zassert_equal(999, config->k_sample, "k_sample is %d",
		      config->k_sample);
}

ZTEST(dps_config, test_console_cmd__setkwindow)
{
	struct dps_config_t *config = dps_get_config();

	/* Try some invalid requests first */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkwin"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps setkwin -1"), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps setkwin 4"), NULL);

	zassert_equal(4, config->k_window, "k_window is %d", config->k_window);
}

ZTEST(dps_config, test_console_cmd__settcheck)
{
	struct dps_config_t *config = dps_get_config();

	/* Try some invalid requests first */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps settcheck"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps settcheck -1"),
		   NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps settcheck 5"), NULL);

	zassert_equal(5 * USEC_PER_SEC, config->t_check, "t_check is %d",
		      config->t_check);
}

ZTEST(dps_config, test_console_cmd__settstable)
{
	struct dps_config_t *config = dps_get_config();

	/* Try some invalid requests first */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps settstable"), NULL);
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps settstable -1"),
		   NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "dps settstable 6"), NULL);

	zassert_equal(6 * USEC_PER_SEC, config->t_stable, "t_stable is %d",
		      config->t_stable);
}

ZTEST(dps_config, test_console_cmd__invalid)
{
	/* Non-existent subcommand should fail */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "dps foobar xyz"), NULL);
}

ZTEST_SUITE(dps_config, drivers_predicate_pre_main, dps_config_setup,
	    dps_config_before, dps_config_after, NULL);
