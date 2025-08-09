/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "drivers/imvp/rt3645.h"
#include "emul/emul_rt3645.h"
#include "power.h"
#include "test/drivers/test_state.h"
#include "timer.h"

#include <stdlib.h>
#include <time.h>

#include <zephyr/fff.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define RT3645_PORT 0
#define RT3645_NODE DT_NODELABEL(rt3645_emul)

extern int rt3645_get_flag(int port);

const struct emul *emul = EMUL_DT_GET(RT3645_NODE);
const struct device *dev = DEVICE_DT_GET(RT3645_NODE);

#define RT3645_PAGE_MAX 0x0D

/* Generate random non-zero page number */
#define RT3645_GEN_RAND_PAGE() ((rand() % RT3645_PAGE_MAX) + 1);

/* Generate random non-zero register value */
#define RT3645_GEN_RAND_REG_VAL() ((rand() % 0xFF) + 1);
/**
 * @brief Send correct sequence to RT3645 and set it in config mode
 *
 * @retval 0 if successful, otherwise if it fails
 */
int rt3645_imvp_set_in_config_mode(void)
{
	return shell_execute_cmd(get_ec_shell(),
				 "imvp cfg_mode 0x24 0x25 0x26 0x27");
}

ZTEST(rt3645_imvp, test_product_id)
{
	uint8_t reg_val;

	rt3645_emul_read_reg(emul, PRODUCT_ID_REG, &reg_val);
	zassert_equal(reg_val, 0x45);
}

ZTEST(rt3645_imvp, test_config_mode)
{
	zassert_false(rt3645_emul_in_config_mode(emul));

	/* Setting wrong pattern length */
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "imvp cfg_mode 0x24 0x25 0x26"),
		   NULL);
	zassert_ok(rt3645_set_page(dev, 4));
	zassert_false(rt3645_emul_in_config_mode(emul));

	/* Setting wrong pattern */
	zassert_ok(shell_execute_cmd(get_ec_shell(),
				     "imvp cfg_mode 0x24 0x25 0x26 0x30"),
		   NULL);
	zassert_false(rt3645_emul_in_config_mode(emul));

	/* Set register to reset internal device counter */
	zassert_ok(rt3645_set_page(dev, 4));

	/* Setting correct pattern out of G3/S5 */
	power_set_state(POWER_S0);
	zassert_not_ok(rt3645_imvp_set_in_config_mode());

	/* Setting correct pattern */
	power_set_state(POWER_S5);
	zassert_ok(rt3645_imvp_set_in_config_mode());
	zassert_true(rt3645_emul_in_config_mode(emul));
}

/**
 * @brief Write registers into RT3645 page using console command
 *
 * @param  page Number of page to write registers
 * @param  regs Array holding registers value to be written into device, if set
 *              to NULL, only page number is set.
 * @param  count Number of bytes to be written, if set to 0, only page number
 *               is set.
 *
 * @retval 0 if successful, otherwise if it fails
 */
int rt3645_imvp_test_fill_page_regs(uint8_t page, uint8_t *regs, uint8_t count)
{
	int ret;
	char set_page_cmd_in[32] = { 0 };
	char set_regs_cmd_in[128] = { 0 };
	char reg_val_str_in[6] = { 0 };
	const char *set_reg_cmd = "imvp set_regs 0";
	const char *set_page_cmd = "imvp set_page 0x%X";
	const char *set_reg_cat = " 0x%X";

	sprintf(set_page_cmd_in, set_page_cmd, page);
	ret = shell_execute_cmd(get_ec_shell(), set_page_cmd_in);
	if (ret) {
		return ret;
	}

	if (regs == NULL || count == 0) {
		/* Not required to write any registers, just page */
		return 0;
	}

	strcpy(set_regs_cmd_in, set_reg_cmd);
	for (int i = 0; i < count; i++) {
		reg_val_str_in[0] = '\0';
		sprintf(reg_val_str_in, set_reg_cat, regs[i]);
		strcat(set_regs_cmd_in, reg_val_str_in);
	}

	return shell_execute_cmd(get_ec_shell(), set_regs_cmd_in);
}

ZTEST(rt3645_imvp, test_page_setting)
{
	uint8_t page_in;
	uint8_t page_out;

	zassert_ok(rt3645_imvp_set_in_config_mode());
	zassert_true(rt3645_emul_in_config_mode(emul));

	rt3645_emul_read_reg(emul, PAGE_SET_REG, &page_out);
	zassert_equal(page_out, 0);

	/* Generate random page */
	page_in = RT3645_GEN_RAND_PAGE();

	/* Attempt to change page while out of G3/S5 */
	power_set_state(POWER_S0);
	zassert_not_ok(rt3645_imvp_test_fill_page_regs(page_in, NULL, 0));
	zassert_ok(rt3645_emul_read_reg(emul, PAGE_SET_REG, &page_out));
	zassert_not_equal(page_in, page_out);

	power_set_state(POWER_G3);
	zassert_ok(rt3645_imvp_test_fill_page_regs(page_in, NULL, 0));
	zassert_ok(rt3645_emul_read_reg(emul, PAGE_SET_REG, &page_out));
	zassert_equal(page_in, page_out);
}

#define RT3654_IMVP_TEST_SET_REGS_COUNT 5

ZTEST(rt3645_imvp, test_setting_page_regs)
{
	uint8_t page_in;
	uint8_t regs_in[RT3654_IMVP_TEST_SET_REGS_COUNT];

	/* Set random page and generate random registers value */
	page_in = RT3645_GEN_RAND_PAGE();
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		regs_in[i] = RT3645_GEN_RAND_REG_VAL();
	}

	zassert_ok(rt3645_imvp_test_fill_page_regs(
		page_in, regs_in, RT3654_IMVP_TEST_SET_REGS_COUNT));

	/* Test setting registers while not in config mode */
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		uint8_t reg_out;

		zassert_ok(rt3645_emul_read_reg(emul, i, &reg_out));
		zassert_not_equal(reg_out, regs_in[i]);
	}

	/* Enter config mode */
	zassert_ok(rt3645_imvp_set_in_config_mode());
	zassert_true(rt3645_emul_in_config_mode(emul));
	zassert_ok(rt3645_imvp_test_fill_page_regs(
		page_in, regs_in, RT3654_IMVP_TEST_SET_REGS_COUNT));

	/* Test setting registers while in config mode */
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		uint8_t reg_out;

		zassert_ok(rt3645_emul_read_reg(emul, i, &reg_out));
		zassert_equal(reg_out, regs_in[i]);
	}
}

ZTEST(rt3645_imvp, test_dump_regs)
{
	const char *buffer;
	size_t buffer_size;
	uint8_t page_in;
	int page = -1;
	uint8_t regs_in[RT3654_IMVP_TEST_SET_REGS_COUNT];

	/* Allow shell thread to start and get console output */
	k_msleep(10);

	/* Set random page and generate random registers value */
	page_in = RT3645_GEN_RAND_PAGE();
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		regs_in[i] = RT3645_GEN_RAND_REG_VAL();
	}

	/* Enter config mode */
	zassert_ok(rt3645_imvp_set_in_config_mode());
	zassert_true(rt3645_emul_in_config_mode(emul));
	zassert_ok(rt3645_imvp_test_fill_page_regs(
		page_in, regs_in, RT3654_IMVP_TEST_SET_REGS_COUNT));

	/* Test out of required power state */
	power_set_state(POWER_S0);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "imvp dump_regs"));
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_not_null(strstr(
		buffer, "To access paged registers put system on G3/S5"));

	power_set_state(POWER_G3);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp dump_regs"));
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_is_null(strstr(
		buffer, "To access paged registers put system on G3/S5"));

	/* Test page number */
	buffer = strstr(buffer, "Page: 0x");
	sscanf(buffer, "Page: 0x%X", &page);
	zassert_equal(page_in, page);

	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		char pattern[32];
		int reg_out;

		zassert_not_equal(sprintf(pattern, "[%02Xh]", i), 0);
		buffer = strstr(buffer, pattern);
		zassert_not_null(buffer);
		sscanf(buffer, "[%*02Xh] = 0x%02X", &reg_out);
		zassert_equal(regs_in[i], reg_out);
	}
}

uint8_t g_page_in;
uint8_t g_regs_in[RT3654_IMVP_TEST_SET_REGS_COUNT];

ZTEST(rt3645_imvp, test_store_load_reg_0)
{
	/* Set random page and generate random registers value */
	g_page_in = RT3645_GEN_RAND_PAGE();
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		g_regs_in[i] = RT3645_GEN_RAND_REG_VAL();
	}

	zassert_ok(rt3645_imvp_set_in_config_mode());
	zassert_true(rt3645_emul_in_config_mode(emul));
	zassert_ok(rt3645_imvp_test_fill_page_regs(
		g_page_in, g_regs_in, RT3654_IMVP_TEST_SET_REGS_COUNT));

	power_set_state(POWER_S0);
	zassert_not_ok(shell_execute_cmd(get_ec_shell(), "imvp store_cfg"));
	power_set_state(POWER_S5);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp store_cfg"));
}

ZTEST(rt3645_imvp, test_store_load_reg_1)
{
	uint8_t regs_in[RT3654_IMVP_TEST_SET_REGS_COUNT];

	/* Set random page and generate random registers value */
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		regs_in[i] = RT3645_GEN_RAND_REG_VAL();
	}

	zassert_ok(rt3645_imvp_set_in_config_mode());
	zassert_true(rt3645_emul_in_config_mode(emul));

	/* Set new set of registers */
	zassert_ok(rt3645_imvp_test_fill_page_regs(
		g_page_in, regs_in, RT3654_IMVP_TEST_SET_REGS_COUNT));

	/* Load earlier registers setting */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "imvp load_cfg"));

	/* Ensure paged registers value is same as earlier */
	for (int i = 0; i < RT3654_IMVP_TEST_SET_REGS_COUNT; i++) {
		uint8_t reg_out;

		zassert_ok(rt3645_read_reg(dev, i, &reg_out));
		zassert_equal(reg_out, g_regs_in[i]);
	}
}

void rt3645_imvp_test_reset(void *fixture)
{
	srand(time(NULL));
	rt3645_emul_reset_regs(emul);
}

ZTEST_SUITE(rt3645_imvp, drivers_predicate_pre_main, NULL,
	    rt3645_imvp_test_reset, NULL, NULL);
